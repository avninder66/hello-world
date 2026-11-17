import { cookies } from "next/headers";
import { NextResponse, type NextRequest } from "next/server";
import { getSessionUser, workspaceAccess } from "@/lib/auth";
import { encryptSecret, pkceChallenge, randomToken } from "@/lib/crypto";
import { appUrl } from "@/lib/env";
import { OAUTH_PROVIDERS, providerConfigured } from "@/lib/oauth/providers";
import { STATE_COOKIE } from "@/lib/oauth/state";

export const runtime = "nodejs";



/** Starts an OAuth connect: /api/connect/linkedin?workspace=<slug> */
export async function GET(request: NextRequest, ctx: RouteContext<"/api/connect/[provider]">) {
  const { provider: id } = await ctx.params;
  const provider = OAUTH_PROVIDERS[id];
  const slug = request.nextUrl.searchParams.get("workspace") ?? "";
  const user = await getSessionUser();
  if (!user) return NextResponse.redirect(`${appUrl()}/login`);
  if (!provider) return NextResponse.json({ error: "Unknown provider" }, { status: 404 });
  if (!providerConfigured(provider)) {
    return NextResponse.json({ error: `${provider.label} isn't configured. Set ${provider.envVars.join(" and ")}.` }, { status: 400 });
  }
  const access = await workspaceAccess(user.id, { slug }, "admin").catch(() => null);
  if (!access) return NextResponse.json({ error: "Only workspace owners/admins can connect accounts" }, { status: 403 });

  const state = randomToken(16);
  const verifier = randomToken(48);
  const redirectUri = `${appUrl()}/api/connect/${id}/callback`;
  (await cookies()).set(
    STATE_COOKIE,
    encryptSecret(JSON.stringify({ state, verifier, provider: id, workspaceId: access.workspace.id, slug, userId: user.id })),
    { httpOnly: true, secure: appUrl().startsWith("https"), sameSite: "lax", maxAge: 600, path: "/api/connect" },
  );
  return NextResponse.redirect(provider.authorizeUrl({ state, codeChallenge: pkceChallenge(verifier), redirectUri }));
}
