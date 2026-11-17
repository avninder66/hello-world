import { connection } from "next/server";
import { supabasePublishableKey, supabaseUrl } from "@/lib/env";

/** Exposes the (public) Supabase URL and publishable key to browser code at runtime. */
export async function PublicConfig() {
  await connection(); // read env at request time, never at build time
  const json = JSON.stringify({ supabaseUrl: supabaseUrl(), supabaseKey: supabasePublishableKey() }).replace(/</g, "\\u003c");
  return <script dangerouslySetInnerHTML={{ __html: `window.__CADENCE__=${json};` }} />;
}
