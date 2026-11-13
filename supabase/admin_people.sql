-- Admin "People" tab: who has signed up and who is online right now. Admin only.
create or replace function public.admin_people() returns jsonb
language plpgsql security definer set search_path = public as $$
begin
  if not public.is_admin() then raise exception 'Not allowed'; end if;
  return coalesce((select jsonb_agg(x order by (x->>'created_at') desc) from (
    select jsonb_build_object(
      'id', p.id, 'username', p.username, 'display_name', p.display_name, 'email', u.email,
      'created_at', p.created_at, 'status', s.status, 'trial_end', s.trial_end,
      'last_seen', a.last_seen, 'online', coalesce(a.last_seen > now() - interval '90 seconds', false),
      'ambassador', exists (select 1 from public.ambassadors m where m.user_id = p.id), 'is_admin', p.is_admin) as x
    from public.profiles p
    left join auth.users u on u.id = p.id
    left join public.subscriptions s on s.user_id = p.id
    left join public.active_sessions a on a.user_id = p.id) t), '[]'::jsonb);
end $$;
revoke all on function public.admin_people() from public, anon;
grant execute on function public.admin_people() to authenticated;
