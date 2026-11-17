import { NextResponse, type NextRequest } from "next/server";
import { userClient } from "@/lib/supabase/server";

/** Exchanges the code from a Supabase magic link / OAuth sign-in for a session. */
export async function GET(request: NextRequest) {
  const code = request.nextUrl.searchParams.get("code");
  const next = request.nextUrl.searchParams.get("next") ?? "/";
  if (code) {
    const supabase = await userClient();
    await supabase.auth.exchangeCodeForSession(code);
  }
  return NextResponse.redirect(new URL(next.startsWith("/") ? next : "/", request.url));
}
