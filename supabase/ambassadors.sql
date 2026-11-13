-- Ambassadors: the list, the black star and the private chat room.
-- Run once in the Supabase dashboard: SQL Editor > New query > paste > Run.

create table if not exists public.ambassadors (
  user_id uuid primary key references public.profiles(id) on delete cascade,
  added_at timestamptz not null default now()
);
alter table public.ambassadors enable row level security;     -- no policies: only you (SQL editor / service role) can change the list

create or replace function public.is_ambassador() returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.ambassadors where user_id = auth.uid())
      or exists (select 1 from public.profiles where id = auth.uid() and is_admin);
$$;

-- Usernames only, so every signed-in member's browser can show the black star.
create or replace view public.ambassador_names as
  select p.username from public.ambassadors a join public.profiles p on p.id = a.user_id;
grant select on public.ambassador_names to authenticated;

create table if not exists public.amb_messages (
  id bigserial primary key,
  author_id uuid not null references public.profiles(id) on delete cascade,
  body text not null check (char_length(body) between 1 and 2000),
  created_at timestamptz not null default now()
);
alter table public.amb_messages enable row level security;

drop policy if exists amb_read on public.amb_messages;
create policy amb_read on public.amb_messages for select to authenticated using (public.is_ambassador());
drop policy if exists amb_write on public.amb_messages;
create policy amb_write on public.amb_messages for insert to authenticated with check (author_id = auth.uid() and public.is_ambassador());
drop policy if exists amb_delete on public.amb_messages;
create policy amb_delete on public.amb_messages for delete to authenticated
  using (public.is_ambassador() and (author_id = auth.uid() or exists (select 1 from public.profiles where id = auth.uid() and is_admin)));

-- Make tg123 an Ambassador. To add someone else later, change the username and run just this line.
insert into public.ambassadors (user_id) select id from public.profiles where username = 'tg123' on conflict do nothing;
