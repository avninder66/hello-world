import { NextResponse, type NextRequest } from "next/server";
import { appUrl } from "@/lib/env";
import { userClient } from "@/lib/supabase/server";

/** Exchanges the code from a Supabase email link / sign-in for a session. */
export async function GET(request: NextRequest) {
  const code = request.nextUrl.searchParams.get("code");
  const next = request.nextUrl.searchParams.get("next") ?? "/";
  if (code) {
    const supabase = await userClient();
    await supabase.auth.exchangeCodeForSession(code);
  }
  // Redirect using APP_URL: behind cPanel/Passenger the request URL carries the
  // server's internal hostname, not the public domain. Only allow local paths.
  const path = next.startsWith("/") && !next.startsWith("//") ? next : "/";
  return NextResponse.redirect(`${appUrl()}${path}`);
}
