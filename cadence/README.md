# Cadence

A multi-client social media scheduler (in the spirit of Buffer) built so **Claude can draft and schedule posts** — with a person approving them before anything goes live.

- **Workspaces per client/brand**: each with its own connected accounts, calendar, team, brand voice and approval rule.
- **7 networks**: LinkedIn (profiles and company pages), X, Facebook Pages, Instagram (business), Threads, Bluesky and Mastodon.
- **Composer**: one post, per-account wording, live character counts the way each network counts, images/video with alt text, previews.
- **Approval workflow**: posts from Claude, the API or editors wait in *Approvals*. Owners/admins approve or request changes. Claude can't approve.
- **Calendar & queue**: month view in the workspace's timezone, with retries for failed posts.
- **Claude, two ways**
  - **MCP server** at `/api/mcp`: Claude Code / Claude Desktop can read the brand voice, see the calendar, and create, edit and schedule posts.
  - **"Write with Claude"** in the composer drafts a version for each selected network from a short brief.
- **Publisher**: a cron endpoint publishes due posts, refreshes tokens, retries rate limits and 5xx errors with backoff, and marks accounts that need reconnecting.

## How it fits together

```
Claude (MCP) ─┐                    ┌─> LinkedIn / X / Facebook / Instagram
REST API ─────┼─> Next.js app ─────┤    Threads / Bluesky / Mastodon
Web UI ───────┘   (posts service:  │
                   validation +    └── /api/cron/publish (every minute)
                   approval rules)          │
                        │                   │
                        └──── Supabase ─────┘
                 (Postgres + RLS, Auth, Storage for media)
```

All three entry points go through `src/lib/posts.ts`, so validation and approval rules are the same everywhere. Approval logic is in `src/lib/workflow.ts`; each network is an adapter in `src/lib/platforms/`.

**Approval rules** (when *Require approval* is on, the default): posts created by Claude/MCP, the REST API, or an *editor* go to `pending_approval`. Owners and admins in the web app schedule directly. Editing a scheduled post through MCP sends it back for approval.

**Roles**: owner · admin (approves, connects accounts, settings) · editor (writes) · viewer (read only — useful for clients).

**Security**: OAuth tokens are encrypted with AES-256-GCM (`TOKEN_ENCRYPTION_KEY`) and can't be read by browser clients, even by members (column grants + RLS). API keys are stored only as SHA-256 hashes. OAuth state is kept in an encrypted, httpOnly cookie and X uses PKCE.

## Setup

### 1. Supabase

1. Create a project at supabase.com.
2. Apply the schema: `npx supabase link --project-ref <ref>` then `npx supabase db push` (or paste `supabase/migrations/*.sql` into the SQL editor).
3. Authentication → URL configuration: set the Site URL to your app URL and add `<APP_URL>/auth/callback` to redirect URLs.

### 2. Environment

Copy `.env.example` to `.env.local` and fill it in. Generate secrets with:

```bash
openssl rand -base64 32   # TOKEN_ENCRYPTION_KEY
openssl rand -hex 24      # CRON_SECRET
```

Keep `TOKEN_ENCRYPTION_KEY` safe: changing it means every account must reconnect.

### 3. Social network apps

You only need the networks you use; unconfigured ones show "Not set up". For each, the redirect URI is `<APP_URL>/api/connect/<provider>/callback`.

| Network | Where | Notes |
|---|---|---|
| LinkedIn (`linkedin`) | linkedin.com/developers → Create app → add *Sign In with LinkedIn using OpenID Connect* and *Share on LinkedIn* | Company pages need the *Community Management API* product; then set `LINKEDIN_SCOPES` to include `w_organization_social r_organization_admin`. Tokens last 60 days. |
| X (`x`) | developer.x.com → Project → App → User authentication: OAuth 2.0, type *Web App* | Posting needs a paid API tier. |
| Facebook + Instagram (`meta`) | developers.facebook.com → Create app (Business) → add *Facebook Login for Business* | Instagram must be a Business/Creator account linked to a Facebook Page. Needs Meta app review for other people's accounts. |
| Threads (`threads`) | developers.facebook.com → Create app → *Access the Threads API* | Long-lived tokens are refreshed automatically. |
| Bluesky | none | Users connect with a handle + app password. |
| Mastodon | none | Users paste an access token from their server (scopes `write:statuses write:media`). |

### 4. Run locally

```bash
npm install
npx supabase start        # optional: local Supabase in Docker (uses supabase/config.toml)
npm run dev
```

With `PUBLISH_DRY_RUN=true` the whole pipeline runs without posting anywhere — useful for trying the workflow before connecting real accounts.

To publish due posts locally, call the cron endpoint:

```bash
curl -H "Authorization: Bearer $CRON_SECRET" http://localhost:3000/api/cron/publish
```

### 5. Deploy

Deploy to Vercel (set the same environment variables). Due posts are published by calling `/api/cron/publish` every minute:

- **Vercel Pro**: `vercel.json` already schedules it (Vercel sends `CRON_SECRET` automatically).
- **Vercel Hobby** (cron limited to daily) or any other host: run `supabase/publisher-cron.sql` in Supabase to call it from `pg_cron` every minute.

## Connecting Claude (MCP)

1. In the app, open **Claude & API** and create an API key (optionally limited to one client workspace).
2. Claude Code:
   ```bash
   claude mcp add --transport http cadence https://YOUR-APP/api/mcp --header "Authorization: Bearer cad_..."
   ```
   Claude Desktop (`claude_desktop_config.json`):
   ```json
   { "mcpServers": { "cadence": { "command": "npx", "args": ["-y", "mcp-remote", "https://YOUR-APP/api/mcp", "--header", "Authorization:Bearer cad_..."] } } }
   ```
3. Ask: *"Plan next week for Acme Bakery: three LinkedIn and Instagram posts about the Christmas menu, Tue/Thu/Sat at 9am."*

**Tools**: `list_workspaces`, `get_workspace` (brand voice, timezone, account ids, platform rules), `check_post`, `list_posts`, `get_post`, `create_post`, `update_post`, `delete_post`. There's deliberately no approve tool.

## REST API

Same API keys (`Authorization: Bearer cad_...`):

- `GET /api/v1/workspaces`
- `GET /api/v1/workspaces/:id/accounts`
- `GET /api/v1/workspaces/:id/posts?status=scheduled,pending_approval&from=&to=`
- `POST /api/v1/workspaces/:id/posts` `{ content, accountIds, overrides?, media?, scheduledAt?, intent? }`

## Development

```bash
npm test           # unit tests: rules, counting, timezones, encryption, every adapter (mocked network), AI request
npm run typecheck
npm run lint
```

## Known limits

- Video is supported for Instagram (Reels), Threads and Mastodon; LinkedIn, X and Bluesky are text + images for now.
- LinkedIn posts take one image; X up to 4.
- No analytics yet (likes, reach). The adapter structure makes this the natural next step.
- Instagram/Facebook need images at a public URL; uploads go to the public Supabase `media` bucket.
