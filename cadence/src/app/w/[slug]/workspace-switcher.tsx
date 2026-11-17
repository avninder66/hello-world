"use client";

import { useRouter } from "next/navigation";

export function WorkspaceSwitcher({ current, workspaces }: { current: string; workspaces: { slug: string; name: string }[] }) {
  const router = useRouter();
  return (
    <select
      aria-label="Workspace"
      className="input font-medium"
      value={current}
      onChange={(e) => router.push(e.target.value === "__new" ? "/workspaces/new" : `/w/${e.target.value}`)}
    >
      {workspaces.map((w) => <option key={w.slug} value={w.slug}>{w.name}</option>)}
      <option value="__new">+ New workspace…</option>
    </select>
  );
}
