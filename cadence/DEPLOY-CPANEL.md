# Hosting Cadence on GoDaddy (cPanel)

Cadence runs as a Node.js app on your GoDaddy hosting, with its data in Supabase (a free cloud database). You upload one zip file. You don't need to build anything on the server.

**Time needed:** about an hour the first time, most of it spent on the Supabase and social-network setup.

---

## Step 0 — Check your plan can run Node.js

1. Log in to GoDaddy → **My Products** → **Web Hosting** → **Manage** → **cPanel Admin**.
2. In cPanel, type **node** in the search box at the top.

- **You see "Setup Node.js App"** → continue with Step 1.
- **Nothing appears** → your plan can't run this app as it is. Ask GoDaddy support: *"Can Node.js apps (Setup Node.js App / CloudLinux Node.js Selector) be enabled on my cPanel plan, and with Node.js version 20 or newer?"* If they say no, your options are:
  - **Upgrade** to a GoDaddy plan that has it, or a **GoDaddy VPS with cPanel** (instructions at the end).
  - **Keep your domain at GoDaddy, host the app on Vercel** (free tier is fine) and point the domain at it. This is often the easiest route; see the main README → Deploy.

The app needs **Node.js 20.9 or newer** (choose 22 if offered).

---

## Step 1 — Create the Supabase database

1. Sign up at supabase.com and create a project (pick a region near you, e.g. London). Save the database password somewhere safe.
2. Open **SQL Editor** → **New query**, paste the whole of `supabase/migrations/20261117000000_init.sql`, and click **Run**. It should say "Success".
3. Open **Project Settings → API keys** and copy:
   - Project URL → `SUPABASE_URL`
   - Publishable key (`sb_publishable_…`) → `SUPABASE_PUBLISHABLE_KEY`
   - Secret key (`sb_secret_…`) → `SUPABASE_SECRET_KEY` (keep this private)
4. **Authentication → URL Configuration**:
   - Site URL: `https://yourdomain.com`
   - Redirect URLs: add `https://yourdomain.com/auth/callback`

---

## Step 2 — Back up your current website

The app will take over **yourdomain.com**, so anything there now will stop showing.

1. cPanel → **File Manager** → open `public_html`.
2. Select everything → **Compress** → Zip → download the zip to your computer.

(If you'd rather keep the current site, use a subdomain like `social.yourdomain.com` instead. Create it in cPanel → **Domains** first, then use it everywhere below.)

---

## Step 3 — Upload the app

1. Get `cadence-cpanel.zip`. Either:
   - download the latest from GitHub → **Actions** → **Cadence – build cPanel package** → latest run → **Artifacts**. GitHub wraps it in another zip, so unzip that once on your computer.
   - or build it yourself: `cd cadence && npm install && npm run package:cpanel` (creates `dist/cadence-cpanel.zip`).
2. cPanel → **File Manager** → click **Home** so you're in `/home/<your-user>`. **Not** `public_html` or the domain's folder (e.g. `public_html/yourdomain.com`): everything there is public.
3. **Upload** `cadence-cpanel.zip`, then right-click it → **Extract**, with the destination set to your home folder only. You'll get a folder called `cadence` containing `app.js` (about 1 KB), `package.json` and `server/`.
4. Delete the zip.

> Do Step 3 **before** Step 4. When cPanel creates a Node.js app in an empty folder it puts a sample `app.js` there that just shows "It works!". If that happens, replace its contents with `deploy/cpanel/app.js` from this repo (or the `app.js` inside the zip) and restart.

---

## Step 4 — Create the Node.js app

cPanel → **Setup Node.js App** → **Create Application**:

| Field | Value |
|---|---|
| Node.js version | 22 (or the highest 20+) |
| Application mode | Production |
| Application root | `cadence` |
| Application URL | `yourdomain.com` (leave the path empty) |
| Application startup file | `app.js` |

Under **Environment variables**, click **Add Variable** for each of these:

| Name | Value |
|---|---|
| `APP_URL` | `https://yourdomain.com` (no trailing slash) |
| `SUPABASE_URL` | from Step 1 |
| `SUPABASE_PUBLISHABLE_KEY` | from Step 1 |
| `SUPABASE_SECRET_KEY` | from Step 1 |
| `TOKEN_ENCRYPTION_KEY` | a random secret, see below |
| `CRON_SECRET` | a different random secret, see below |
| `ANTHROPIC_API_KEY` | optional: enables the "Write with Claude" button |

Social network keys (`LINKEDIN_CLIENT_ID`, `X_CLIENT_ID`, `META_APP_ID`, …) can be added later; see `.env.example` for the full list.

**Making the random secrets:** in cPanel → **Terminal** (if your plan has it) run `openssl rand -base64 32` for `TOKEN_ENCRYPTION_KEY` and `openssl rand -hex 24` for `CRON_SECRET`. Or use any password generator: 40+ random characters for `CRON_SECRET`; `TOKEN_ENCRYPTION_KEY` must be exactly 32 bytes in base64, so use the openssl command for that one. **Keep `TOKEN_ENCRYPTION_KEY` safe and never change it**: if it changes, every social account has to be reconnected.

> Alternative to the variables screen: copy `cadence/.env.example` to `cadence/.env` in File Manager and fill it in. `app.js` reads it on start. Never put this file in `public_html`.

Click **Create**, then **Restart**. You **don't** need to click "Run NPM Install": everything the app needs is already in `server/`.

Now open `https://yourdomain.com`. You should see the Cadence sign-in page. Create your account.

**HTTPS:** GoDaddy cPanel issues free certificates through **SSL/TLS Status → Run AutoSSL**. The app must be on `https://` for logins and the social networks to work.

---

## Step 5 — Turn on automatic publishing

Scheduled posts go out when something calls `https://yourdomain.com/api/cron/publish` with your `CRON_SECRET`. Use Supabase for this, not cPanel. Many shared hosts silently stretch cPanel cron jobs to every 5–15 minutes; Supabase can run it every minute.

1. Open `supabase/publisher-cron.sql`, replace `REPLACE_WITH_CRON_SECRET` with your `CRON_SECRET` and `https://YOUR-APP.vercel.app` with `https://yourdomain.com`.
2. Paste it into Supabase **SQL Editor** and **Run**.

To check it's working: write a post scheduled a couple of minutes ahead, wait, and it should switch to *Published* (or *Failed* with a reason).

*Fallback:* cPanel → **Cron Jobs**, set it to every minute (or the shortest interval GoDaddy allows) with this command:
`curl -s -H "Authorization: Bearer YOUR_CRON_SECRET" https://yourdomain.com/api/cron/publish > /dev/null`

---

## Step 6 — Connect social networks and Claude

- For each network's developer app, the redirect URL is `https://yourdomain.com/api/connect/<provider>/callback` (providers: `linkedin`, `x`, `meta`, `threads`). See the table in `README.md`. Add the keys as environment variables in Setup Node.js App, then **Restart**.
- Bluesky and Mastodon need no developer app.
- Claude: in the app, open **Claude & API**, create a key, and follow the instructions there. The MCP address is `https://yourdomain.com/api/mcp`.

---

## Updating to a new version

1. Download the new `cadence-cpanel.zip` (Step 3).
2. In File Manager, delete the `cadence/server` folder (leave `app.js` and your `.env`, if you made one).
3. Upload the zip to your home folder and extract it. Choose to overwrite, which replaces `server/`.
4. Setup Node.js App → **Restart**.

If an update includes a new file in `supabase/migrations/`, run it in the Supabase SQL Editor first.

---

## Troubleshooting

| Problem | What to do |
|---|---|
| "Incomplete response" / 503 | Open `cadence/stderr.log` in File Manager. A line like `Missing environment variable X` means a variable is missing; add it and Restart. |
| Error creating the app about `node_modules` | Make sure the **Application root** is `cadence` (the folder with `app.js`), not `cadence/server`. |
| Sign-in link or social connect goes to the wrong address | Check `APP_URL` and the Supabase Site URL/redirect URL match `https://yourdomain.com` exactly. |
| Posts stay *Scheduled* past their time | The publishing timer isn't running: redo Step 5. You can trigger it by hand with the curl command above. |
| App is slow to load the first time after a while | Normal on shared hosting: the app sleeps when idle and takes a few seconds to wake. |
| Out of memory / app keeps restarting | Shared plans have small memory limits. Ask GoDaddy about the plan's limit, or move to a VPS. |
| Site shows "It works! NodeJS …" | The app is running cPanel's sample `app.js`. Replace the contents of `app.js` in the application root with Cadence's (see Step 3) and Restart. |
| 403 Forbidden on the whole site | The domain isn't linked to the app any more (e.g. after deleting/recreating it). Edit the app → Save → Restart, or recreate it. |
| "No such file or directory: …/.htaccess" when stopping/deleting the app | Create an empty `.htaccess` in the domain's folder, then retry. |
| "Specified directory already used by …" when creating the app | An app is still registered on that folder: edit it instead, or rename the folder and create the app with the new name. |
| Publishing calls get `{"error":"unauthorised"}` | `CRON_SECRET` in cPanel and the `cadence_cron_secret` stored in Supabase differ. Make them identical and Restart. |
| The domain's `.htaccess` has Passenger lines | Those are added by cPanel to route your domain to the app. Leave them. |

---

## GoDaddy VPS with cPanel/WHM

If the Node.js app manager isn't available, ask GoDaddy (or your server admin) to enable **CloudLinux Node.js Selector** or **Application Manager** (WHM → Software → EasyApache 4 → Node.js + Passenger), then follow the steps above. Without it, run `node app.js` under a process manager (e.g. `pm2`) with `PORT=3000`, and proxy the domain to `127.0.0.1:3000` in Apache.
