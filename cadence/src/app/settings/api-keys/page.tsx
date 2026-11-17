import Link from "next/link";
import { listUserWorkspaces, requireUser } from "@/lib/auth";
import { appUrl } from "@/lib/env";
import { userClient } from "@/lib/supabase/server";
import { CreateKeyForm, RevokeButton } from "./forms";

export default async function ApiKeysPage() {
  const user = await requireUser();
  const supabase = await userClient();
  const [{ data: keys }, workspaces] = await Promise.all([
    supabase.from("api_keys").select("id, name, key_prefix, workspace_id, last_used_at, revoked_at, created_at").order("created_at", { ascending: false }),
    listUserWorkspaces(user.id),
  ]);
  const mcpUrl = `${appUrl()}/api/mcp`;

  return (
    <main className="mx-auto max-w-3xl space-y-8 px-4 py-10">
      <div>
        <Link href="/" className="text-sm text-muted hover:text-fg">← Back</Link>
        <h1 className="h1 mt-2">Claude &amp; API</h1>
        <p className="text-sm text-muted">Let Claude draft, schedule and review posts for your workspaces through MCP.</p>
      </div>

      <section className="card space-y-3 p-5">
        <h2 className="h2">1. Create an API key</h2>
        <p className="text-sm text-muted">A key acts as you. Restrict it to one client workspace if you like. Keys are shown once.</p>
        <CreateKeyForm workspaces={workspaces.map((w) => ({ id: w.id, name: w.name }))} />
      </section>

      <section className="card space-y-3 p-5 text-sm">
        <h2 className="h2">2. Connect Claude</h2>
        <p className="font-medium">Claude Code</p>
        <pre className="overflow-x-auto rounded-lg bg-surface-2 p-3 text-xs">{`claude mcp add --transport http cadence ${mcpUrl} \\
  --header "Authorization: Bearer cad_YOUR_KEY"`}</pre>
        <p className="font-medium">Claude Desktop</p>
        <p className="text-muted">Settings → Developer → Edit config, then add:</p>
        <pre className="overflow-x-auto rounded-lg bg-surface-2 p-3 text-xs">{JSON.stringify(
          { mcpServers: { cadence: { command: "npx", args: ["-y", "mcp-remote", mcpUrl, "--header", "Authorization:Bearer cad_YOUR_KEY"] } } },
          null, 2)}</pre>
        <p className="font-medium">Then ask Claude things like</p>
        <ul className="list-disc space-y-1 pl-5 text-muted">
          <li>“Plan next week for Acme Bakery: three posts on LinkedIn and Instagram about the Christmas menu, Tue/Thu/Sat at 9am.”</li>
          <li>“What&apos;s scheduled for all my clients this week? Flag any gaps.”</li>
          <li>“Rewrite the rejected posts using the reviewer&apos;s notes.”</li>
        </ul>
        <p className="hint">Claude can&apos;t approve posts. In workspaces that require approval, a person approves them here first.</p>
      </section>

      <section className="space-y-3">
        <h2 className="h2">Your keys</h2>
        {!keys?.length ? (
          <p className="text-sm text-muted">No keys yet.</p>
        ) : (
          <ul className="card divide-y divide-line text-sm">
            {keys.map((k) => (
              <li key={k.id} className={`flex flex-wrap items-center gap-3 px-4 py-3 ${k.revoked_at ? "opacity-50" : ""}`}>
                <div className="min-w-0 flex-1">
                  <p className="font-medium">{k.name} <code className="text-xs text-muted">{k.key_prefix}…</code></p>
                  <p className="text-xs text-muted">
                    {k.workspace_id ? workspaces.find((w) => w.id === k.workspace_id)?.name ?? "One workspace" : "All workspaces"}
                    {" · "}{k.last_used_at ? `last used ${new Date(k.last_used_at).toLocaleString("en-GB")}` : "never used"}
                    {k.revoked_at && " · revoked"}
                  </p>
                </div>
                {!k.revoked_at && <RevokeButton id={k.id} />}
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}
