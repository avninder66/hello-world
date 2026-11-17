import { PlatformIcon } from "@/components/ui";
import { requireWorkspacePage, roleAtLeast } from "@/lib/auth";
import { OAUTH_PROVIDERS, providerConfigured } from "@/lib/oauth/providers";
import { PLATFORM_SPECS } from "@/lib/platforms/limits";
import { listAccounts } from "@/lib/posts";
import { CredentialForms, DisconnectButton } from "./forms";

export default async function AccountsPage({ params, searchParams }: PageProps<"/w/[slug]/accounts">) {
  const { slug } = await params;
  const sp = await searchParams;
  const access = await requireWorkspacePage(slug);
  const accounts = await listAccounts(access);
  const canAdmin = roleAtLeast(access.role, "admin");

  return (
    <div className="mx-auto max-w-3xl space-y-8">
      <div>
        <h1 className="h1">Accounts</h1>
        <p className="text-sm text-muted">Social profiles {access.workspace.name} posts to.</p>
      </div>
      {typeof sp.connected === "string" && <p className="rounded-lg bg-ok-soft px-4 py-2 text-sm text-ok">{sp.connected}</p>}
      {typeof sp.error === "string" && <p className="rounded-lg bg-bad-soft px-4 py-2 text-sm text-bad">{sp.error}</p>}

      <section className="space-y-3">
        <h2 className="h2">Connected</h2>
        {accounts.length === 0 ? (
          <p className="text-sm text-muted">No accounts yet.</p>
        ) : (
          <ul className="card divide-y divide-line">
            {accounts.map((a) => (
              <li key={a.id} className="flex items-center gap-3 px-4 py-3">
                <PlatformIcon platform={a.platform} size={28} />
                <div className="min-w-0 flex-1">
                  <p className="truncate font-medium">{a.display_name}</p>
                  <p className="truncate text-xs text-muted">
                    {PLATFORM_SPECS[a.platform].label}
                    {a.handle ? ` · @${a.handle}` : ""}
                    {a.token_expires_at && a.platform !== "x" ? ` · access until ${new Date(a.token_expires_at).toLocaleDateString("en-GB")}` : ""}
                  </p>
                  {a.status === "needs_reauth" && <p className="text-xs text-bad">Needs reconnecting: {a.last_error}</p>}
                </div>
                {canAdmin && <DisconnectButton slug={slug} accountId={a.id} />}
              </li>
            ))}
          </ul>
        )}
      </section>

      {canAdmin && (
        <section className="space-y-3">
          <h2 className="h2">Connect</h2>
          <div className="grid gap-3 sm:grid-cols-2">
            {Object.values(OAUTH_PROVIDERS).map((p) => {
              const ready = providerConfigured(p);
              return (
                <div key={p.id} className="card flex items-center gap-3 p-4">
                  <span className="flex gap-1">{p.platforms.map((pl) => <PlatformIcon key={pl} platform={pl} size={24} />)}</span>
                  <div className="min-w-0 flex-1">
                    <p className="font-medium">{p.label}</p>
                    {!ready && <p className="hint">Set {p.envVars.join(", ")}</p>}
                    {p.id === "meta" && ready && <p className="hint">Pages you manage + linked Instagram business accounts</p>}
                  </div>
                  {ready ? (
                    <a className="btn btn-sm" href={`/api/connect/${p.id}?workspace=${slug}`}>Connect</a>
                  ) : (
                    <span className="btn btn-sm opacity-50">Not set up</span>
                  )}
                </div>
              );
            })}
          </div>
          <CredentialForms slug={slug} />
        </section>
      )}
    </div>
  );
}
