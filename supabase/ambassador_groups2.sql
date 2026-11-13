-- Ambassadors can add their accepted friends to a group chat. Friends who are not Ambassadors can read and post in that group only.

create or replace function public.is_friend_of_me(u uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.friendships f where f.status = 'accepted'
    and ((f.requester_id = auth.uid() and f.addressee_id = u) or (f.addressee_id = auth.uid() and f.requester_id = u)));
$$;

drop policy if exists gc_msg_write on public.gc_messages;
create policy gc_msg_write on public.gc_messages for insert to authenticated
  with check (author_id = auth.uid() and public.is_gc_member(group_id));

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
    select g, p.id from public.profiles p
    where p.id = any(coalesce(ids, '{}')) and p.id <> auth.uid()
      and (exists (select 1 from public.ambassador_people a where a.id = p.id) or public.is_friend_of_me(p.id))
    on conflict do nothing;
  return g;
end $$;

create or replace function public.gc_add(g bigint, u uuid) returns void
language plpgsql security definer set search_path = public as $$
begin
  if not exists (select 1 from public.gc_members where group_id = g and user_id = auth.uid() and is_owner) then raise exception 'Only the group owner can add people'; end if;
  if not (exists (select 1 from public.ambassador_people where id = u) or public.is_friend_of_me(u)) then raise exception 'You can add Ambassadors and your friends'; end if;
  insert into public.gc_members (group_id, user_id) values (g, u) on conflict do nothing;
end $$;
