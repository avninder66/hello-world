"use client";

import { useActionState, useTransition } from "react";
import { addMember, removeMember, updateWorkspace } from "@/app/actions";
import { TimezoneSelect } from "@/components/timezone-select";
import type { Workspace } from "@/lib/types";

export function SettingsForm({ slug, workspace }: { slug: string; workspace: Workspace }) {
  const [state, action, pending] = useActionState(updateWorkspace.bind(null, slug), null);
  return (
    <form action={action} className="card space-y-4 p-5">
      <h2 className="h2">Workspace</h2>
      <div>
        <label className="label" htmlFor="name">Name</label>
        <input id="name" name="name" className="input" defaultValue={workspace.name} />
      </div>
      <div>
        <label className="label" htmlFor="timezone">Timezone</label>
        <TimezoneSelect id="timezone" name="timezone" defaultValue={workspace.timezone} />
      </div>
      <div>
        <label className="label" htmlFor="brand_voice">Brand voice &amp; guidelines</label>
        <textarea id="brand_voice" name="brand_voice" className="input min-h-40" defaultValue={workspace.brand_voice}
          placeholder={"Who the client is, audience, tone, words to use/avoid, hashtags, posting rules.\ne.g. Independent bakery in Leeds. Warm, playful, never salesy. British spelling. Always mention we're dog friendly. No emojis on LinkedIn."} />
        <p className="hint mt-1">Claude reads this before writing any post for this workspace — in the app and over MCP.</p>
      </div>
      <label className="flex items-start gap-2 text-sm">
        <input type="checkbox" name="require_approval" defaultChecked={workspace.require_approval} className="mt-1" />
        <span>
          <span className="font-medium">Require approval</span>
          <span className="block text-muted">Posts written by Claude, the API, or editors wait for an owner/admin to approve them before they publish. Recommended.</span>
        </span>
      </label>
      <button className="btn btn-primary" disabled={pending}>{pending ? "Saving…" : "Save"}</button>
      {state && (state.ok ? <span className="ml-3 text-sm text-ok">Saved</span> : <p className="text-sm text-bad">{state.error}</p>)}
    </form>
  );
}

export function MembersForm({ slug, members }: { slug: string; members: { userId: string; role: string; email: string; you: boolean }[] }) {
  const [state, action, pending] = useActionState(addMember.bind(null, slug), null);
  const [removing, start] = useTransition();
  return (
    <section className="card space-y-4 p-5">
      <h2 className="h2">Team</h2>
      <ul className="divide-y divide-line text-sm">
        {members.map((m) => (
          <li key={m.userId} className="flex items-center gap-3 py-2">
            <span className="min-w-0 flex-1 truncate">{m.email}{m.you && <span className="text-muted"> (you)</span>}</span>
            <span className="capitalize text-muted">{m.role}</span>
            {m.role !== "owner" && !m.you && (
              <button className="btn btn-sm btn-danger" disabled={removing} onClick={() => start(async () => { await removeMember(slug, m.userId); })}>Remove</button>
            )}
          </li>
        ))}
      </ul>
      <form action={action} className="flex flex-wrap gap-2">
        <input name="email" type="email" required className="input min-w-0 flex-1" placeholder="colleague@example.com" />
        <select name="role" className="input w-auto" defaultValue="editor">
          <option value="admin">Admin — approves &amp; connects accounts</option>
          <option value="editor">Editor — writes posts</option>
          <option value="viewer">Viewer — read only (e.g. the client)</option>
        </select>
        <button className="btn" disabled={pending}>{pending ? "Adding…" : "Add"}</button>
      </form>
      {state && !state.ok && <p className="text-sm text-bad">{state.error}</p>}
      <p className="hint">New people get an email invite.</p>
    </section>
  );
}
