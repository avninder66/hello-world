import Link from "next/link";
import { redirect } from "next/navigation";
import { listUserWorkspaces, requireUser } from "@/lib/auth";

export default async function Home() {
  const user = await requireUser();
  const workspaces = await listUserWorkspaces(user.id);
  if (workspaces.length === 0) redirect("/workspaces/new");
  if (workspaces.length === 1) redirect(`/w/${workspaces[0].slug}`);
  return (
    <main className="mx-auto max-w-2xl px-4 py-12">
      <div className="mb-6 flex items-center justify-between">
        <h1 className="h1">Your workspaces</h1>
        <Link href="/workspaces/new" className="btn btn-primary">New workspace</Link>
      </div>
      <ul className="card divide-y divide-line">
        {workspaces.map((w) => (
          <li key={w.id}>
            <Link href={`/w/${w.slug}`} className="flex items-center justify-between px-4 py-3 hover:bg-surface-2">
              <span className="font-medium">{w.name}</span>
              <span className="text-xs capitalize text-muted">{w.role}</span>
            </Link>
          </li>
        ))}
      </ul>
    </main>
  );
}
