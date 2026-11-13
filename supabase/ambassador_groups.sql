-- Ambassador group chats (like Instagram groups, no calls). Only Ambassadors can create, join and read them.

create or replace view public.ambassador_people as
  select p.id, p.username, p.display_name, p.avatar_color
  from public.profiles p
  where exists (select 1 from public.ambassadors a where a.user_id = p.id) or p.is_admin;
grant select on public.ambassador_people to authenticated;

create table if not exists public.gc_groups (
  id bigserial primary key,
  name text not null check (char_length(name) between 1 and 60),
  created_by uuid not null references public.profiles(id) on delete cascade,
  created_at timestamptz not null default now()
);
create table if not exists public.gc_members (
  group_id bigint not null references public.gc_groups(id) on delete cascade,
  user_id uuid not null references public.profiles(id) on delete cascade,
  is_owner boolean not null default false,
  joined_at timestamptz not null default now(),
  primary key (group_id, user_id)
);
create table if not exists public.gc_messages (
  id bigserial primary key,
  group_id bigint not null references public.gc_groups(id) on delete cascade,
  author_id uuid not null references public.profiles(id) on delete cascade,
  body text not null check (char_length(body) between 1 and 2000),
  created_at timestamptz not null default now()
);
create index if not exists gc_messages_group on public.gc_messages (group_id, id);
alter table public.gc_groups enable row level security;
alter table public.gc_members enable row level security;
alter table public.gc_messages enable row level security;

create or replace function public.is_gc_member(g bigint) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.gc_members where group_id = g and user_id = auth.uid());
$$;

drop policy if exists gc_groups_read on public.gc_groups;
create policy gc_groups_read on public.gc_groups for select to authenticated using (public.is_gc_member(id));
drop policy if exists gc_members_read on public.gc_members;
create policy gc_members_read on public.gc_members for select to authenticated using (public.is_gc_member(group_id));
drop policy if exists gc_msg_read on public.gc_messages;
create policy gc_msg_read on public.gc_messages for select to authenticated using (public.is_gc_member(group_id));
drop policy if exists gc_msg_write on public.gc_messages;
create policy gc_msg_write on public.gc_messages for insert to authenticated with check (author_id = auth.uid() and public.is_gc_member(group_id) and public.is_ambassador());
drop policy if exists gc_msg_delete on public.gc_messages;
create policy gc_msg_delete on public.gc_messages for delete to authenticated using (author_id = auth.uid() and public.is_gc_member(group_id));

-- Create a group and add people (Ambassadors only).
create or replace function public.gc_create(n text, ids uuid[]) returns bigint
language plpgsql security definer set search_path = public as $$
declare g bigint;
begin
  if not public.is_ambassador() then raise exception 'Only Ambassadors can make groups'; end if;
  n := btrim(coalesce(n, ''));
  if char_length(n) < 1 then raise exception 'Give the group a name'; end if;
  insert into public.gc_groups (name, created_by) values (left(n, 60), auth.uid()) returning id into g;
  insert into public.gc_members (group_id, user_id, is_owner) values (g, auth.uid(), true);
  insert into public.gc_members (group_id, user_id)
    select g, p.id from public.ambassador_people p where p.id = any(coalesce(ids, '{}')) and p.id <> auth.uid()
    on conflict do nothing;
  return g;
end $$;

-- Owner adds another Ambassador.
create or replace function public.gc_add(g bigint, u uuid) returns void
language plpgsql security definer set search_path = public as $$
begin
  if not exists (select 1 from public.gc_members where group_id = g and user_id = auth.uid() and is_owner) then raise exception 'Only the group owner can add people'; end if;
  if not exists (select 1 from public.ambassador_people where id = u) then raise exception 'Only Ambassadors can join'; end if;
  insert into public.gc_members (group_id, user_id) values (g, u) on conflict do nothing;
end $$;

-- Leave a group (an empty group is removed).
create or replace function public.gc_leave(g bigint) returns void
language plpgsql security definer set search_path = public as $$
begin
  delete from public.gc_members where group_id = g and user_id = auth.uid();
  if not exists (select 1 from public.gc_members where group_id = g) then delete from public.gc_groups where id = g; end if;
end $$;

grant execute on function public.gc_create(text, uuid[]) to authenticated;
grant execute on function public.gc_add(bigint, uuid) to authenticated;
grant execute on function public.gc_leave(bigint) to authenticated;
