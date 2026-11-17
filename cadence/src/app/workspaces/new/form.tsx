"use client";

import { useActionState } from "react";
import { createWorkspace } from "@/app/actions";
import { TimezoneSelect } from "@/components/timezone-select";

export function NewWorkspaceForm() {
  const [state, action, pending] = useActionState(createWorkspace, null);
  return (
    <form action={action} className="card space-y-4 p-5">
      <div>
        <label className="label" htmlFor="name">Client or brand name</label>
        <input id="name" name="name" required className="input" placeholder="Acme Bakery" />
      </div>
      <div>
        <label className="label" htmlFor="slug">URL name <span className="font-normal text-muted">(optional)</span></label>
        <input id="slug" name="slug" className="input" placeholder="acme-bakery" pattern="[a-z0-9][a-z0-9-]{1,48}" />
      </div>
      <div>
        <label className="label" htmlFor="timezone">Timezone</label>
        <TimezoneSelect id="timezone" name="timezone" />
      </div>
      {state && !state.ok && <p className="text-sm text-bad">{state.error}</p>}
      <button className="btn btn-primary w-full" disabled={pending}>{pending ? "Creating…" : "Create workspace"}</button>
    </form>
  );
}
