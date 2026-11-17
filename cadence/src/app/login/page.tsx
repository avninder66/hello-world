import { LoginForm } from "./login-form";

export default function LoginPage() {
  return (
    <main className="flex min-h-screen items-center justify-center px-4">
      <div className="w-full max-w-sm">
        <h1 className="h1 mb-1">Cadence</h1>
        <p className="mb-6 text-sm text-muted">Plan, approve and schedule social posts for every client.</p>
        <LoginForm />
      </div>
    </main>
  );
}
