"use client";
import { createBrowserClient } from "@supabase/ssr";

declare global {
  interface Window {
    __CADENCE__?: { supabaseUrl: string; supabaseKey: string };
  }
}

/**
 * Browser Supabase client. The URL and publishable key are injected by the
 * root layout at request time (see PublicConfig), so one build works with
 * any Supabase project — handy for uploading a pre-built zip to cPanel.
 */
export function browserClient() {
  const cfg = window.__CADENCE__;
  if (!cfg) throw new Error("Cadence config missing from page");
  return createBrowserClient(cfg.supabaseUrl, cfg.supabaseKey);
}
