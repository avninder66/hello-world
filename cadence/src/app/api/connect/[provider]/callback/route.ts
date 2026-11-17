import { cookies } from "next/headers";
import { NextResponse, type NextRequest } from "next/server";
import { getSessionUser, workspaceAccess } from "@/lib/auth";
import { decryptSecret, encryptOptional, encryptSecret } from "@/lib/crypto";
import { appUrl } from "@/lib/env";
import { OAUTH_PROVIDERS } from "@/lib/oauth/providers";
import { adminClient } from "@/lib/supabase/admin";
import { STATE_COOKIE } from "@/lib/oauth/state";

export const runtime = "nodejs";

export async function GET(request: NextRequest, ctx: RouteContext<"/api/connect/[provider]/callback">) {
  const { provider: id } = await ctx.params;
  const params = request.nextUrl.searchParams;
  const jar = await cookies();
  const raw = jar.get(STATE_COOKIE)?.value;
  jar.delete({ name: STATE_COOKIE, path: "/api/connect" });

  let saved: { state: string; verifier: string; provider: string; workspaceId: string; slug: string; userId: string };
  try {
    saved = JSON.parse(decryptSecret(raw ?? ""));
  } catch {
    return NextResponse.json({ error: "Connect session expired; please try again." }, { status: 400 });
  }
  const back = (msg: string, ok = false) =>
    NextResponse.redirect(`${appUrl()}/w/${saved.slug}/accounts?${ok ? "connected" : "error"}=${encodeURIComponent(msg)}`);

  const user = await getSessionUser();
  if (!user || user.id !== saved.userId || saved.provider !== id || params.get("state") !== saved.state) {
    return back("Connection could not be verified. Please try again.");
  }
  if (params.get("error")) return back(params.get("error_description") ?? params.get("error")!);
  const provider = OAUTH_PROVIDERS[id];
  const code = params.get("code");
  if (!provider || !code) return back("Missing authorisation code.");

  try {
    await workspaceAccess(user.id, { id: saved.workspaceId }, "admin");
    const accounts = await provider.exchange(
      { code, codeVerifier: saved.verifier, redirectUri: `${appUrl()}/api/connect/${id}/callback` },
      fetch,
    );
    if (!accounts.length) return back(`No ${provider.label} accounts were found to connect.`);
    const { error } = await adminClient()
      .from("social_accounts")
      .upsert(
        accounts.map((a) => ({
          workspace_id: saved.workspaceId,
          platform: a.platform,
          external_id: a.externalId,
          display_name: a.displayName,
          handle: a.handle,
          avatar_url: a.avatarUrl,
          access_token_enc: encryptSecret(a.accessToken),
          refresh_token_enc: encryptOptional(a.refreshToken),
          token_expires_at: a.expiresAt?.toISOString() ?? null,
          meta: a.meta,
          status: "active",
          last_error: null,
        })),
        { onConflict: "workspace_id,platform,external_id" },
      );
    if (error) throw error;
    return back(`Connected ${accounts.map((a) => a.displayName).join(", ")}`, true);
  } catch (err) {
    console.error("OAuth callback failed", err);
    return back(err instanceof Error ? err.message : "Connection failed");
  }
}
