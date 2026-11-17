import Link from "next/link";
import { signOut } from "@/app/actions";
import { listUserWorkspaces, requireWorkspacePage } from "@/lib/auth";
import { adminClient } from "@/lib/supabase/admin";
import { NavLinks } from "./nav-links";
import { WorkspaceSwitcher } from "./workspace-switcher";

export default async function WorkspaceLayout({ children, params }: LayoutProps<"/w/[slug]">) {
  const { slug } = await params;
  const { user, workspace, role } = await requireWorkspacePage(slug);
  const [workspaces, pending] = await Promise.all([
    listUserWorkspaces(user.id),
    adminClient().from("posts").select("id", { count: "exact", head: true }).eq("workspace_id", workspace.id).eq("status", "pending_approval"),
  ]);

  return (
    <div className="flex min-h-screen flex-col md:flex-row">
      <aside className="flex shrink-0 flex-col gap-4 border-b border-line bg-surface p-4 md:sticky md:top-0 md:h-screen md:w-60 md:border-b-0 md:border-r">
        <Link href="/" className="text-lg font-semibold tracking-tight">Cadence</Link>
        <WorkspaceSwitcher current={slug} workspaces={workspaces.map((w) => ({ slug: w.slug, name: w.name }))} />
        <NavLinks slug={slug} pendingCount={pending.count ?? 0} canAdmin={role === "owner" || role === "admin"} />
        <div className="mt-auto hidden space-y-2 border-t border-line pt-4 text-xs text-muted md:block">
          <Link href="/settings/api-keys" className="block hover:text-fg">API keys &amp; Claude</Link>
          <p className="truncate">{user.email} · <span className="capitalize">{role}</span></p>
          <form action={signOut}><button className="hover:text-fg">Sign out</button></form>
        </div>
      </aside>
      <main className="min-w-0 flex-1 px-4 py-6 md:px-8">{children}</main>
    </div>
  );
}
