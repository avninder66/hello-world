"use client";

import { useRouter } from "next/navigation";
import { useState } from "react";
import { browserClient } from "@/lib/supabase/browser";

export function LoginForm() {
  const router = useRouter();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [mode, setMode] = useState<"password" | "link">("password");
  const [message, setMessage] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    setBusy(true);
    setMessage(null);
    const supabase = browserClient();
    if (mode === "link") {
      const { error } = await supabase.auth.signInWithOtp({
        email,
        options: { emailRedirectTo: `${window.location.origin}/auth/callback` },
      });
      setMessage(error ? error.message : "Check your email for a sign-in link.");
    } else {
      let { error } = await supabase.auth.signInWithPassword({ email, password });
      if (error?.message.toLowerCase().includes("invalid login")) {
        // First visit: create the account.
        const signUp = await supabase.auth.signUp({
          email, password, options: { emailRedirectTo: `${window.location.origin}/auth/callback` },
        });
        error = signUp.error;
        if (!error && !signUp.data.session) {
          setMessage("Account created. Confirm your email, then sign in.");
          setBusy(false);
          return;
        }
      }
      if (error) setMessage(error.message);
      else router.replace("/");
    }
    setBusy(false);
  }

  return (
    <form onSubmit={submit} className="card space-y-4 p-5">
      <div>
        <label className="label" htmlFor="email">Email</label>
        <input id="email" type="email" required className="input" value={email} onChange={(e) => setEmail(e.target.value)} />
      </div>
      {mode === "password" && (
        <div>
          <label className="label" htmlFor="password">Password</label>
          <input id="password" type="password" required minLength={8} className="input" value={password} onChange={(e) => setPassword(e.target.value)} />
          <p className="hint mt-1">New here? Enter a password and we&apos;ll create your account.</p>
        </div>
      )}
      <button className="btn btn-primary w-full" disabled={busy}>
        {busy ? "Working…" : mode === "link" ? "Email me a link" : "Sign in"}
      </button>
      <button type="button" className="w-full text-xs text-muted underline" onClick={() => setMode(mode === "link" ? "password" : "link")}>
        {mode === "link" ? "Use a password instead" : "Email me a sign-in link instead"}
      </button>
      {message && <p className="text-sm text-muted" role="status">{message}</p>}
    </form>
  );
}
