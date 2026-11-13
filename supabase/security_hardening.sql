-- Security hardening. Safe to run more than once.
-- 1) Close the write hole on the two Ambassador views (they ran with owner rights and could be written to).
-- 2) Signed-out visitors get no database access at all.
-- 3) Signed-in members only get the exact write permissions the site uses (row rules stay on top).
-- 4) Functions can only be called by signed-in users, and not at all for trigger functions.
-- 5) Rate limits on the chat rooms.

-- 1) views: read only
revoke all on public.ambassador_names, public.ambassador_people from anon, authenticated;
grant select on public.ambassador_names, public.ambassador_people to authenticated;

-- 2) signed out: nothing
revoke all on all tables in schema public from anon;
revoke all on all sequences in schema public from anon;
alter default privileges for role postgres in schema public revoke all on tables from anon;
alter default privileges for role postgres in schema public revoke all on sequences from anon;
alter default privileges for role postgres in schema public revoke execute on functions from anon, public;

-- 3) signed in: no dangerous table powers, and writes only where the site needs them
revoke truncate, references, trigger on all tables in schema public from authenticated;
revoke insert, update, delete on public.subscriptions, public.customers, public.app_config, public.ambassadors,
  public.gc_groups, public.gc_members, public.shared_songs, public.drops, public.devices from authenticated;
revoke insert, delete on public.active_sessions from authenticated;
revoke update on public.likes, public.gc_messages, public.amb_messages from authenticated;

revoke insert on public.posts from authenticated;
grant insert (author_id, category, title, body) on public.posts to authenticated;
revoke insert on public.comments from authenticated;
grant insert (post_id, author_id, parent_id, body) on public.comments to authenticated;
revoke insert on public.messages from authenticated;
grant insert (sender_id, recipient_id, body) on public.messages to authenticated;
revoke insert on public.friendships from authenticated;
grant insert (requester_id, addressee_id) on public.friendships to authenticated;
revoke insert on public.reports from authenticated;
grant insert (reporter_id, target_type, target_id, reason) on public.reports to authenticated;

-- 4) functions
do $$
declare r record;
begin
  for r in select p.oid::regprocedure as sig, p.prorettype::regtype::text as rt
           from pg_proc p join pg_namespace n on n.oid = p.pronamespace where n.nspname = 'public' loop
    execute format('revoke all on function %s from public, anon', r.sig);
    if r.rt = 'trigger' then execute format('revoke all on function %s from authenticated', r.sig);
    else execute format('grant execute on function %s to authenticated', r.sig); end if;
  end loop;
end $$;
alter function public.current_session() set search_path = public;

-- 5) rate limits (uses the existing rate_limit trigger: column, per hour)
drop trigger if exists amb_rate on public.amb_messages;
create trigger amb_rate before insert on public.amb_messages for each row execute function public.rate_limit('author_id', '200');
drop trigger if exists gc_rate on public.gc_messages;
create trigger gc_rate before insert on public.gc_messages for each row execute function public.rate_limit('author_id', '300');

-- private uploads: cap file size at 100 MB
update storage.buckets set file_size_limit = 104857600 where name = 'drops';
