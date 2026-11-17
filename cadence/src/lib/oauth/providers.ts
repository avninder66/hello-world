import type { Platform } from "../types";
import { GRAPH_VERSION } from "../platforms/meta";
import { readJson, type FetchLike } from "../platforms/types";

/** An account discovered during an OAuth connect, ready to be stored. */
export interface ConnectedAccount {
  platform: Platform;
  externalId: string;
  displayName: string;
  handle: string | null;
  avatarUrl: string | null;
  accessToken: string;
  refreshToken: string | null;
  expiresAt: Date | null;
  meta: Record<string, unknown>;
}

export interface OAuthProvider {
  id: string;
  label: string;
  platforms: Platform[];
  envVars: [string, string];
  usesPkce: boolean;
  authorizeUrl(p: { state: string; codeChallenge: string; redirectUri: string }): string;
  exchange(p: { code: string; codeVerifier: string; redirectUri: string }, fetchImpl: FetchLike): Promise<ConnectedAccount[]>;
}

const env = (name: string) => process.env[name] ?? "";
const expiry = (seconds?: number) => (seconds ? new Date(Date.now() + seconds * 1000) : null);
const form = (params: Record<string, string>) => ({
  method: "POST",
  headers: { "Content-Type": "application/x-www-form-urlencoded" },
  body: new URLSearchParams(params),
});

const linkedin: OAuthProvider = {
  id: "linkedin",
  label: "LinkedIn",
  platforms: ["linkedin"],
  envVars: ["LINKEDIN_CLIENT_ID", "LINKEDIN_CLIENT_SECRET"],
  usesPkce: false,
  authorizeUrl: ({ state, redirectUri }) =>
    `https://www.linkedin.com/oauth/v2/authorization?${new URLSearchParams({
      response_type: "code",
      client_id: env("LINKEDIN_CLIENT_ID"),
      redirect_uri: redirectUri,
      state,
      // Add w_organization_social + r_organization_admin once LinkedIn approves
      // your app for the Community Management API to post as company pages.
      scope: env("LINKEDIN_SCOPES") || "openid profile w_member_social",
    })}`,
  async exchange({ code, redirectUri }, fetchImpl) {
    const tok = await readJson<{ access_token: string; expires_in: number; refresh_token?: string; refresh_token_expires_in?: number }>(
      await fetchImpl("https://www.linkedin.com/oauth/v2/accessToken", form({
        grant_type: "authorization_code",
        code,
        redirect_uri: redirectUri,
        client_id: env("LINKEDIN_CLIENT_ID"),
        client_secret: env("LINKEDIN_CLIENT_SECRET"),
      })),
      "LinkedIn token exchange",
    );
    const me = await readJson<{ sub: string; name: string; picture?: string }>(
      await fetchImpl("https://api.linkedin.com/v2/userinfo", { headers: { Authorization: `Bearer ${tok.access_token}` } }),
      "LinkedIn profile",
    );
    const base = { accessToken: tok.access_token, refreshToken: tok.refresh_token ?? null, expiresAt: expiry(tok.expires_in) };
    const accounts: ConnectedAccount[] = [{
      platform: "linkedin", externalId: me.sub, displayName: me.name, handle: null, avatarUrl: me.picture ?? null,
      meta: { urn: `urn:li:person:${me.sub}`, kind: "person" }, ...base,
    }];

    // Company pages the user administers (only works with organization scopes).
    try {
      const acls = await fetchImpl(
        "https://api.linkedin.com/rest/organizationAcls?q=roleAssignee&role=ADMINISTRATOR&state=APPROVED",
        { headers: { Authorization: `Bearer ${tok.access_token}`, "LinkedIn-Version": "202511", "X-Restli-Protocol-Version": "2.0.0" } },
      );
      if (acls.ok) {
        const json = (await acls.json()) as { elements?: { organization: string }[] };
        for (const el of json.elements ?? []) {
          const orgId = el.organization.split(":").pop()!;
          const org = await fetchImpl(`https://api.linkedin.com/rest/organizations/${orgId}`, {
            headers: { Authorization: `Bearer ${tok.access_token}`, "LinkedIn-Version": "202511" },
          });
          const info = org.ok ? ((await org.json()) as { localizedName?: string; vanityName?: string }) : {};
          accounts.push({
            platform: "linkedin", externalId: `org-${orgId}`, displayName: info.localizedName ?? `Organization ${orgId}`,
            handle: info.vanityName ?? null, avatarUrl: null, meta: { urn: el.organization, kind: "organization" }, ...base,
          });
        }
      }
    } catch {
      /* organization access is optional */
    }
    return accounts;
  },
};

const x: OAuthProvider = {
  id: "x",
  label: "X",
  platforms: ["x"],
  envVars: ["X_CLIENT_ID", "X_CLIENT_SECRET"],
  usesPkce: true,
  authorizeUrl: ({ state, codeChallenge, redirectUri }) =>
    `https://x.com/i/oauth2/authorize?${new URLSearchParams({
      response_type: "code",
      client_id: env("X_CLIENT_ID"),
      redirect_uri: redirectUri,
      scope: "tweet.read tweet.write users.read media.write offline.access",
      state,
      code_challenge: codeChallenge,
      code_challenge_method: "S256",
    })}`,
  async exchange({ code, codeVerifier, redirectUri }, fetchImpl) {
    const basic = Buffer.from(`${env("X_CLIENT_ID")}:${env("X_CLIENT_SECRET")}`).toString("base64");
    const tok = await readJson<{ access_token: string; refresh_token?: string; expires_in: number }>(
      await fetchImpl("https://api.x.com/2/oauth2/token", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded", Authorization: `Basic ${basic}` },
        body: new URLSearchParams({
          grant_type: "authorization_code", code, redirect_uri: redirectUri, code_verifier: codeVerifier, client_id: env("X_CLIENT_ID"),
        }),
      }),
      "X token exchange",
    );
    const me = await readJson<{ data: { id: string; name: string; username: string; profile_image_url?: string } }>(
      await fetchImpl("https://api.x.com/2/users/me?user.fields=profile_image_url", {
        headers: { Authorization: `Bearer ${tok.access_token}` },
      }),
      "X profile",
    );
    return [{
      platform: "x", externalId: me.data.id, displayName: me.data.name, handle: me.data.username,
      avatarUrl: me.data.profile_image_url ?? null, accessToken: tok.access_token,
      refreshToken: tok.refresh_token ?? null, expiresAt: expiry(tok.expires_in), meta: {},
    }];
  },
};

/** One Facebook Login connects every Page the user manages plus linked Instagram accounts. */
const meta: OAuthProvider = {
  id: "meta",
  label: "Facebook & Instagram",
  platforms: ["facebook", "instagram"],
  envVars: ["META_APP_ID", "META_APP_SECRET"],
  usesPkce: false,
  authorizeUrl: ({ state, redirectUri }) =>
    `https://www.facebook.com/${GRAPH_VERSION}/dialog/oauth?${new URLSearchParams({
      client_id: env("META_APP_ID"),
      redirect_uri: redirectUri,
      state,
      response_type: "code",
      scope: [
        "pages_show_list", "pages_read_engagement", "pages_manage_posts",
        "instagram_basic", "instagram_content_publish", "business_management",
      ].join(","),
    })}`,
  async exchange({ code, redirectUri }, fetchImpl) {
    const G = `https://graph.facebook.com/${GRAPH_VERSION}`;
    const short = await readJson<{ access_token: string }>(
      await fetchImpl(`${G}/oauth/access_token?${new URLSearchParams({
        client_id: env("META_APP_ID"), client_secret: env("META_APP_SECRET"), redirect_uri: redirectUri, code,
      })}`),
      "Facebook token exchange",
    );
    // Long-lived user token => page tokens that don't expire.
    const long = await readJson<{ access_token: string }>(
      await fetchImpl(`${G}/oauth/access_token?${new URLSearchParams({
        grant_type: "fb_exchange_token", client_id: env("META_APP_ID"), client_secret: env("META_APP_SECRET"),
        fb_exchange_token: short.access_token,
      })}`),
      "Facebook long-lived token",
    );
    const pages = await readJson<{
      data: {
        id: string; name: string; access_token: string; picture?: { data?: { url?: string } };
        instagram_business_account?: { id: string; username?: string; profile_picture_url?: string };
      }[];
    }>(
      await fetchImpl(`${G}/me/accounts?${new URLSearchParams({
        fields: "id,name,access_token,picture{url},instagram_business_account{id,username,profile_picture_url}",
        limit: "100",
        access_token: long.access_token,
      })}`),
      "Facebook pages",
    );
    const out: ConnectedAccount[] = [];
    for (const page of pages.data) {
      out.push({
        platform: "facebook", externalId: page.id, displayName: page.name, handle: null,
        avatarUrl: page.picture?.data?.url ?? null, accessToken: page.access_token, refreshToken: null, expiresAt: null,
        meta: {},
      });
      const ig = page.instagram_business_account;
      if (ig) {
        out.push({
          platform: "instagram", externalId: ig.id, displayName: ig.username ?? page.name, handle: ig.username ?? null,
          avatarUrl: ig.profile_picture_url ?? null, accessToken: page.access_token, refreshToken: null, expiresAt: null,
          meta: { facebook_page_id: page.id },
        });
      }
    }
    return out;
  },
};

const threads: OAuthProvider = {
  id: "threads",
  label: "Threads",
  platforms: ["threads"],
  envVars: ["THREADS_APP_ID", "THREADS_APP_SECRET"],
  usesPkce: false,
  authorizeUrl: ({ state, redirectUri }) =>
    `https://threads.net/oauth/authorize?${new URLSearchParams({
      client_id: env("THREADS_APP_ID"),
      redirect_uri: redirectUri,
      scope: "threads_basic,threads_content_publish",
      response_type: "code",
      state,
    })}`,
  async exchange({ code, redirectUri }, fetchImpl) {
    const short = await readJson<{ access_token: string; user_id: string | number }>(
      await fetchImpl("https://graph.threads.net/oauth/access_token", form({
        client_id: env("THREADS_APP_ID"), client_secret: env("THREADS_APP_SECRET"),
        grant_type: "authorization_code", redirect_uri: redirectUri, code,
      })),
      "Threads token exchange",
    );
    const long = await readJson<{ access_token: string; expires_in: number }>(
      await fetchImpl(`https://graph.threads.net/access_token?${new URLSearchParams({
        grant_type: "th_exchange_token", client_secret: env("THREADS_APP_SECRET"), access_token: short.access_token,
      })}`),
      "Threads long-lived token",
    );
    const me = await readJson<{ id: string; username: string; threads_profile_picture_url?: string }>(
      await fetchImpl(`https://graph.threads.net/v1.0/me?${new URLSearchParams({
        fields: "id,username,threads_profile_picture_url", access_token: long.access_token,
      })}`),
      "Threads profile",
    );
    return [{
      platform: "threads", externalId: me.id, displayName: me.username, handle: me.username,
      avatarUrl: me.threads_profile_picture_url ?? null, accessToken: long.access_token, refreshToken: null,
      expiresAt: expiry(long.expires_in), meta: {},
    }];
  },
};

export const OAUTH_PROVIDERS: Record<string, OAuthProvider> = { linkedin, x, meta, threads };

export function providerConfigured(p: OAuthProvider): boolean {
  return p.envVars.every((v) => !!process.env[v]);
}
