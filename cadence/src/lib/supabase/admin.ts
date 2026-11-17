import "server-only";
import { createClient, type SupabaseClient } from "@supabase/supabase-js";
import { requireEnv, supabaseUrl } from "../env";

let cached: SupabaseClient | null = null;

/**
 * Service-role client. Bypasses row level security, so every caller must
 * check permissions itself (see lib/auth.ts). Never import from client code.
 */
export function adminClient(): SupabaseClient {
  if (!cached) {
    cached = createClient(supabaseUrl(), requireEnv("SUPABASE_SECRET_KEY"), {
      auth: { persistSession: false, autoRefreshToken: false },
    });
  }
  return cached;
}
