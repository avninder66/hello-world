import { createServerClient } from "@supabase/ssr";
import { NextResponse, type NextRequest } from "next/server";
import { appUrl, supabasePublishableKey, supabaseUrl } from "@/lib/env";

/** Keeps the Supabase auth session fresh and sends signed-out users to /login. */
export async function proxy(request: NextRequest) {
  let response = NextResponse.next({ request });
  const supabase = createServerClient(
    supabaseUrl(),
    supabasePublishableKey(),
    {
      cookies: {
        getAll: () => request.cookies.getAll(),
        setAll: (toSet) => {
          for (const { name, value } of toSet) request.cookies.set(name, value);
          response = NextResponse.next({ request });
          for (const { name, value, options } of toSet) response.cookies.set(name, value, options);
        },
      },
    },
  );
  const { data } = await supabase.auth.getUser();
  const path = request.nextUrl.pathname;
  const isPublic = path === "/login" || path.startsWith("/auth/");
  if (!data.user && !isPublic) {
    // APP_URL, not the request URL: behind cPanel/Passenger the request carries
    // the server's internal hostname.
    return NextResponse.redirect(`${appUrl()}/login`);
  }
  return response;
}

export const config = {
  // API routes authenticate themselves (API keys, cron secret, OAuth state).
  matcher: ["/((?!api/|_next/static|_next/image|favicon.ico|.*\\.(?:png|jpg|jpeg|svg|webp|ico)$).*)"],
};
