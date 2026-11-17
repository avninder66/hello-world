"use client";

import { useActionState, useTransition } from "react";
import { connectCredentials, disconnectAccount } from "@/app/actions";
import { PlatformIcon } from "@/components/ui";

export function DisconnectButton({ slug, accountId }: { slug: string; accountId: string }) {
  const [pending, start] = useTransition();
  return (
    <button className="btn btn-sm btn-danger" disabled={pending}
      onClick={() => confirm("Disconnect this account? Its scheduled posts will no longer publish to it.") && start(async () => { await disconnectAccount(slug, accountId); })}>
      Disconnect
    </button>
  );
}

export function CredentialForms({ slug }: { slug: string }) {
  const [bsky, bskyAction, bskyPending] = useActionState(connectCredentials.bind(null, slug), null);
  const [masto, mastoAction, mastoPending] = useActionState(connectCredentials.bind(null, slug), null);
  return (
    <div className="grid gap-3 sm:grid-cols-2">
      <form action={bskyAction} className="card space-y-2 p-4">
        <p className="flex items-center gap-2 font-medium"><PlatformIcon platform="bluesky" size={24} /> Bluesky</p>
        <input type="hidden" name="platform" value="bluesky" />
        <input name="handle" required className="input" placeholder="yourname.bsky.social" />
        <input name="password" required type="password" className="input" placeholder="App password (xxxx-xxxx-xxxx-xxxx)" />
        <p className="hint">Create an app password in Bluesky → Settings → Privacy and security → App passwords. Don&apos;t use your main password.</p>
        <button className="btn btn-sm" disabled={bskyPending}>{bskyPending ? "Checking…" : "Connect Bluesky"}</button>
        {bsky && (bsky.ok ? <p className="text-sm text-ok">Connected.</p> : <p className="text-sm text-bad">{bsky.error}</p>)}
      </form>
      <form action={mastoAction} className="card space-y-2 p-4">
        <p className="flex items-center gap-2 font-medium"><PlatformIcon platform="mastodon" size={24} /> Mastodon</p>
        <input type="hidden" name="platform" value="mastodon" />
        <input name="instance" required className="input" placeholder="mastodon.social" />
        <input name="token" required type="password" className="input" placeholder="Access token" />
        <p className="hint">On your server: Preferences → Development → New application, with the write:statuses and write:media scopes. Copy “Your access token”.</p>
        <button className="btn btn-sm" disabled={mastoPending}>{mastoPending ? "Checking…" : "Connect Mastodon"}</button>
        {masto && (masto.ok ? <p className="text-sm text-ok">Connected.</p> : <p className="text-sm text-bad">{masto.error}</p>)}
      </form>
    </div>
  );
}
