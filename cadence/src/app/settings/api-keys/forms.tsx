"use client";

import { useActionState, useTransition } from "react";
import { createApiKey, revokeApiKey } from "@/app/actions";

export function CreateKeyForm({ workspaces }: { workspaces: { id: string; name: string }[] }) {
  const [state, action, pending] = useActionState(createApiKey, null);
  return (
    <form action={action} className="space-y-3">
      <div className="flex flex-wrap gap-2">
        <input name="name" className="input min-w-0 flex-1" placeholder="Name, e.g. Claude Desktop" />
        <select name="workspace_id" className="input w-auto" defaultValue="">
          <option value="">All my workspaces</option>
          {workspaces.map((w) => <option key={w.id} value={w.id}>Only {w.name}</option>)}
        </select>
        <button className="btn btn-primary" disabled={pending}>{pending ? "Creating…" : "Create key"}</button>
      </div>
      {state?.ok && (
        <div className="rounded-lg bg-ok-soft p-3 text-sm">
          <p className="mb-1 font-medium text-ok">Copy this key now — it won&apos;t be shown again.</p>
          <code className="block break-all rounded bg-surface p-2 text-xs">{state.key}</code>
        </div>
      )}
      {state && !state.ok && <p className="text-sm text-bad">{state.error}</p>}
    </form>
  );
}

export function RevokeButton({ id }: { id: string }) {
  const [pending, start] = useTransition();
  return (
    <button className="btn btn-sm btn-danger" disabled={pending}
      onClick={() => confirm("Revoke this key? Anything using it stops working.") && start(async () => { await revokeApiKey(id); })}>
      Revoke
    </button>
  );
}
