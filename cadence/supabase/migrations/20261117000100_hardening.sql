-- Hardening from the Supabase security/performance advisors.

-- 1. Keep the membership helper out of the public API: move it to a private
--    schema that PostgREST doesn't expose, and point every policy at it.
create schema if not exists private;
grant usage on schema private to authenticated;

create or replace function private.is_workspace_member(ws uuid, min_role public.workspace_role default 'viewer')
returns boolean
language sql stable security definer set search_path = '' as $$
  select exists (
    select 1 from public.workspace_members m
    where m.workspace_id = ws
      and m.user_id = (select auth.uid())
      and case min_role
            when 'viewer' then true
            when 'editor' then m.role in ('owner', 'admin', 'editor')
            when 'admin'  then m.role in ('owner', 'admin')
            when 'owner'  then m.role = 'owner'
          end
  );
$$;
revoke execute on function private.is_workspace_member(uuid, public.workspace_role) from public, anon;
grant execute on function private.is_workspace_member(uuid, public.workspace_role) to authenticated;

drop policy ws_select on public.workspaces;
drop policy ws_update on public.workspaces;
drop policy ws_delete on public.workspaces;
create policy ws_select on public.workspaces for select to authenticated using (private.is_workspace_member(id));
create policy ws_update on public.workspaces for update to authenticated using (private.is_workspace_member(id, 'admin'));
create policy ws_delete on public.workspaces for delete to authenticated using (private.is_workspace_member(id, 'owner'));

drop policy wm_select on public.workspace_members;
drop policy wm_insert on public.workspace_members;
drop policy wm_update on public.workspace_members;
drop policy wm_delete on public.workspace_members;
create policy wm_select on public.workspace_members for select to authenticated using (private.is_workspace_member(workspace_id));
create policy wm_insert on public.workspace_members for insert to authenticated with check (private.is_workspace_member(workspace_id, 'admin'));
create policy wm_update on public.workspace_members for update to authenticated using (private.is_workspace_member(workspace_id, 'admin'));
create policy wm_delete on public.workspace_members for delete to authenticated
  using (private.is_workspace_member(workspace_id, 'admin') or user_id = (select auth.uid()));

drop policy sa_select on public.social_accounts;
drop policy sa_delete on public.social_accounts;
create policy sa_select on public.social_accounts for select to authenticated using (private.is_workspace_member(workspace_id));
create policy sa_delete on public.social_accounts for delete to authenticated using (private.is_workspace_member(workspace_id, 'admin'));

drop policy posts_select on public.posts;
drop policy posts_insert on public.posts;
drop policy posts_update on public.posts;
drop policy posts_delete on public.posts;
create policy posts_select on public.posts for select to authenticated using (private.is_workspace_member(workspace_id));
create policy posts_insert on public.posts for insert to authenticated with check (private.is_workspace_member(workspace_id, 'editor'));
create policy posts_update on public.posts for update to authenticated using (private.is_workspace_member(workspace_id, 'editor'));
create policy posts_delete on public.posts for delete to authenticated using (private.is_workspace_member(workspace_id, 'editor'));

drop policy pt_all on public.post_targets;
create policy pt_all on public.post_targets for all to authenticated
  using (exists (select 1 from public.posts p where p.id = post_id and private.is_workspace_member(p.workspace_id, 'viewer')))
  with check (exists (select 1 from public.posts p where p.id = post_id and private.is_workspace_member(p.workspace_id, 'editor')));

drop policy pe_select on public.post_events;
create policy pe_select on public.post_events for select to authenticated
  using (exists (select 1 from public.posts p where p.id = post_id and private.is_workspace_member(p.workspace_id)));

drop policy ak_select on public.api_keys;
drop policy ak_update on public.api_keys;
create policy ak_select on public.api_keys for select to authenticated using (user_id = (select auth.uid()));
create policy ak_update on public.api_keys for update to authenticated using (user_id = (select auth.uid()));

drop policy media_write on storage.objects;
drop policy media_delete on storage.objects;
create policy media_write on storage.objects for insert to authenticated
  with check (bucket_id = 'media'
              and private.is_workspace_member(((storage.foldername(name))[1])::uuid, 'editor'));
create policy media_delete on storage.objects for delete to authenticated
  using (bucket_id = 'media'
         and private.is_workspace_member(((storage.foldername(name))[1])::uuid, 'editor'));

drop function public.is_workspace_member(uuid, public.workspace_role);

-- 2. Only signed-in users may create workspaces.
revoke execute on function public.create_workspace(text, text, text) from public, anon;
grant execute on function public.create_workspace(text, text, text) to authenticated;

-- 3. Fixed search_path on the trigger function.
alter function public.touch_updated_at() set search_path = '';

-- 4. Indexes for foreign keys.
create index if not exists post_targets_social_account_id_idx on public.post_targets (social_account_id);
create index if not exists api_keys_workspace_id_idx on public.api_keys (workspace_id);
create index if not exists post_events_actor_id_idx on public.post_events (actor_id);
create index if not exists posts_created_by_idx on public.posts (created_by);
create index if not exists posts_approved_by_idx on public.posts (approved_by);
