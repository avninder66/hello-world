-- Cadence: multi-workspace social media scheduler
-- Every row belongs to a workspace (one per client/brand). Access is enforced
-- with row level security based on workspace membership.

create extension if not exists pgcrypto;

-- ---------------------------------------------------------------------------
-- Types
-- ---------------------------------------------------------------------------
create type workspace_role as enum ('owner', 'admin', 'editor', 'viewer');

create type social_platform as enum (
  'linkedin', 'x', 'facebook', 'instagram', 'threads', 'bluesky', 'mastodon'
);

create type post_status as enum (
  'draft',             -- being written, never publishes
  'pending_approval',  -- waiting for an owner/admin to approve
  'scheduled',         -- approved, will publish at scheduled_at
  'publishing',        -- claimed by the publisher
  'published',         -- every target published
  'partially_failed',  -- some targets published, some failed
  'failed',            -- every target failed
  'rejected'           -- an approver rejected it
);

create type target_status as enum ('pending', 'publishing', 'published', 'failed');

create type post_source as enum ('web', 'ai', 'api', 'mcp');

-- ---------------------------------------------------------------------------
-- Tables
-- ---------------------------------------------------------------------------
create table workspaces (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  slug text not null unique check (slug ~ '^[a-z0-9][a-z0-9-]{1,48}$'),
  timezone text not null default 'Europe/London',
  -- Posts written by Claude/API, or by editors, wait for approval when true.
  require_approval boolean not null default true,
  -- Free text Claude reads before writing posts for this client.
  brand_voice text not null default '',
  created_at timestamptz not null default now()
);

create table workspace_members (
  workspace_id uuid not null references workspaces(id) on delete cascade,
  user_id uuid not null references auth.users(id) on delete cascade,
  role workspace_role not null default 'editor',
  created_at timestamptz not null default now(),
  primary key (workspace_id, user_id)
);
create index on workspace_members (user_id);

create table social_accounts (
  id uuid primary key default gen_random_uuid(),
  workspace_id uuid not null references workspaces(id) on delete cascade,
  platform social_platform not null,
  external_id text not null,          -- platform's id for the profile/page
  display_name text not null,
  handle text,
  avatar_url text,
  -- Tokens are AES-256-GCM encrypted by the app (TOKEN_ENCRYPTION_KEY) and
  -- are never readable by browser clients (see column grants below).
  access_token_enc text,
  refresh_token_enc text,
  token_expires_at timestamptz,
  meta jsonb not null default '{}',   -- e.g. mastodon instance URL, person URN
  status text not null default 'active' check (status in ('active', 'needs_reauth', 'disabled')),
  last_error text,
  created_at timestamptz not null default now(),
  unique (workspace_id, platform, external_id)
);

create table posts (
  id uuid primary key default gen_random_uuid(),
  workspace_id uuid not null references workspaces(id) on delete cascade,
  content text not null default '',
  -- [{ "url": "...", "type": "image"|"video", "alt": "..." }]
  media jsonb not null default '[]',
  status post_status not null default 'draft',
  scheduled_at timestamptz,
  source post_source not null default 'web',
  created_by uuid references auth.users(id) on delete set null,
  approved_by uuid references auth.users(id) on delete set null,
  approved_at timestamptz,
  review_note text,
  published_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);
create index on posts (workspace_id, scheduled_at);
create index posts_due_idx on posts (scheduled_at) where status = 'scheduled';

create table post_targets (
  id uuid primary key default gen_random_uuid(),
  post_id uuid not null references posts(id) on delete cascade,
  social_account_id uuid not null references social_accounts(id) on delete cascade,
  content_override text,              -- per-platform wording; null = use post.content
  status target_status not null default 'pending',
  external_id text,
  external_url text,
  error text,
  attempts int not null default 0,
  published_at timestamptz,
  unique (post_id, social_account_id)
);

create table post_events (
  id bigint generated always as identity primary key,
  post_id uuid not null references posts(id) on delete cascade,
  actor_id uuid references auth.users(id) on delete set null,
  actor_label text,                   -- "Claude (MCP)", "publisher", ...
  action text not null,
  detail jsonb not null default '{}',
  created_at timestamptz not null default now()
);
create index on post_events (post_id, created_at);

-- Personal API keys used by the REST API and the MCP server.
-- Only a SHA-256 hash is stored; the key is shown once at creation.
create table api_keys (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  workspace_id uuid references workspaces(id) on delete cascade, -- null = all of the user's workspaces
  name text not null,
  key_prefix text not null,
  key_hash text not null unique,
  last_used_at timestamptz,
  revoked_at timestamptz,
  created_at timestamptz not null default now()
);
create index on api_keys (user_id);

create or replace function touch_updated_at() returns trigger
language plpgsql as $$
begin
  new.updated_at = now();
  return new;
end $$;

create trigger posts_touch before update on posts
for each row execute function touch_updated_at();

-- ---------------------------------------------------------------------------
-- Membership helpers (security definer so policies don't recurse)
-- ---------------------------------------------------------------------------
create or replace function is_workspace_member(ws uuid, min_role workspace_role default 'viewer')
returns boolean
language sql stable security definer set search_path = public as $$
  select exists (
    select 1 from workspace_members m
    where m.workspace_id = ws
      and m.user_id = auth.uid()
      and case min_role
            when 'viewer' then true
            when 'editor' then m.role in ('owner', 'admin', 'editor')
            when 'admin'  then m.role in ('owner', 'admin')
            when 'owner'  then m.role = 'owner'
          end
  );
$$;

-- Creates a workspace and makes the caller its owner, atomically.
create or replace function create_workspace(p_name text, p_slug text, p_timezone text default 'Europe/London')
returns workspaces
language plpgsql security definer set search_path = public as $$
declare
  ws workspaces;
begin
  if auth.uid() is null then
    raise exception 'not authenticated';
  end if;
  insert into workspaces (name, slug, timezone)
  values (p_name, p_slug, coalesce(p_timezone, 'Europe/London'))
  returning * into ws;
  insert into workspace_members (workspace_id, user_id, role)
  values (ws.id, auth.uid(), 'owner');
  return ws;
end $$;

-- Atomically claims due posts for the publisher. SKIP LOCKED makes it safe
-- for overlapping cron runs. Only the service role may call it.
create or replace function claim_due_posts(p_limit int default 20)
returns setof posts
language sql security definer set search_path = public as $$
  update posts p
     set status = 'publishing'
   where p.id in (
     select id from posts
      where status = 'scheduled' and scheduled_at <= now()
      order by scheduled_at
      limit p_limit
      for update skip locked
   )
  returning p.*;
$$;

-- Posts stuck in 'publishing' (e.g. a crashed run) go back to the queue.
create or replace function requeue_stale_posts(p_minutes int default 15)
returns int
language sql security definer set search_path = public as $$
  with r as (
    update posts set status = 'scheduled'
     where status = 'publishing' and updated_at < now() - make_interval(mins => p_minutes)
    returning 1
  )
  select count(*)::int from r;
$$;

revoke execute on function claim_due_posts(int) from public, anon, authenticated;
revoke execute on function requeue_stale_posts(int) from public, anon, authenticated;

-- ---------------------------------------------------------------------------
-- Row level security
-- ---------------------------------------------------------------------------
alter table workspaces enable row level security;
alter table workspace_members enable row level security;
alter table social_accounts enable row level security;
alter table posts enable row level security;
alter table post_targets enable row level security;
alter table post_events enable row level security;
alter table api_keys enable row level security;

create policy ws_select on workspaces for select using (is_workspace_member(id));
create policy ws_update on workspaces for update using (is_workspace_member(id, 'admin'));
create policy ws_delete on workspaces for delete using (is_workspace_member(id, 'owner'));

create policy wm_select on workspace_members for select using (is_workspace_member(workspace_id));
create policy wm_insert on workspace_members for insert with check (is_workspace_member(workspace_id, 'admin'));
create policy wm_update on workspace_members for update using (is_workspace_member(workspace_id, 'admin'));
create policy wm_delete on workspace_members for delete using (is_workspace_member(workspace_id, 'admin') or user_id = auth.uid());

-- Accounts are connected/updated by the server (service role) during OAuth.
create policy sa_select on social_accounts for select using (is_workspace_member(workspace_id));
create policy sa_delete on social_accounts for delete using (is_workspace_member(workspace_id, 'admin'));

create policy posts_select on posts for select using (is_workspace_member(workspace_id));
create policy posts_insert on posts for insert with check (is_workspace_member(workspace_id, 'editor'));
create policy posts_update on posts for update using (is_workspace_member(workspace_id, 'editor'));
create policy posts_delete on posts for delete using (is_workspace_member(workspace_id, 'editor'));

create policy pt_all on post_targets for all
  using (exists (select 1 from posts p where p.id = post_id and is_workspace_member(p.workspace_id, 'viewer')))
  with check (exists (select 1 from posts p where p.id = post_id and is_workspace_member(p.workspace_id, 'editor')));

create policy pe_select on post_events for select
  using (exists (select 1 from posts p where p.id = post_id and is_workspace_member(p.workspace_id)));

create policy ak_select on api_keys for select using (user_id = auth.uid());
create policy ak_update on api_keys for update using (user_id = auth.uid());

-- Browser clients must never read encrypted tokens, even their own.
revoke select on social_accounts from anon, authenticated;
grant select (id, workspace_id, platform, external_id, display_name, handle, avatar_url,
              token_expires_at, meta, status, last_error, created_at)
  on social_accounts to authenticated;
revoke select on api_keys from anon, authenticated;
grant select (id, user_id, workspace_id, name, key_prefix, last_used_at, revoked_at, created_at)
  on api_keys to authenticated;
-- Only the server creates keys; users may only revoke theirs.
revoke insert, update on api_keys from anon, authenticated;
grant update (revoked_at) on api_keys to authenticated;
revoke insert, update on social_accounts from anon, authenticated;

-- ---------------------------------------------------------------------------
-- Storage bucket for post media (public so Instagram/Facebook can fetch it)
-- ---------------------------------------------------------------------------
insert into storage.buckets (id, name, public)
values ('media', 'media', true)
on conflict (id) do nothing;

-- Files live under "<workspace_id>/..."
create policy media_read on storage.objects for select
  using (bucket_id = 'media');
create policy media_write on storage.objects for insert
  with check (bucket_id = 'media'
              and is_workspace_member(((storage.foldername(name))[1])::uuid, 'editor'));
create policy media_delete on storage.objects for delete
  using (bucket_id = 'media'
         and is_workspace_member(((storage.foldername(name))[1])::uuid, 'editor'));

-- Looks up a user by email when adding workspace members (server only).
create or replace function find_user_id_by_email(p_email text)
returns uuid
language sql stable security definer set search_path = public, auth as $$
  select id from auth.users where lower(email) = lower(p_email) limit 1;
$$;
revoke execute on function find_user_id_by_email(text) from public, anon, authenticated;
