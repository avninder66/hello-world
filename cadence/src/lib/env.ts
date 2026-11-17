/** Reads a required environment variable, failing loudly when missing. */
export function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) throw new Error(`Missing environment variable ${name}. See .env.example.`);
  return value;
}

export function appUrl(): string {
  return (process.env.APP_URL ?? "http://localhost:3000").replace(/\/$/, "");
}

export function supabaseUrl() {
  return requireEnv("SUPABASE_URL");
}

export function supabasePublishableKey() {
  return requireEnv("SUPABASE_PUBLISHABLE_KEY");
}
