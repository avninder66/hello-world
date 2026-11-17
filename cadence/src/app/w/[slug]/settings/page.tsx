import { requireWorkspacePage } from "@/lib/auth";
import { adminClient } from "@/lib/supabase/admin";
import { MembersForm, SettingsForm } from "./forms";

export default async function SettingsPage({ params }: PageProps<"/w/[slug]/settings">) {
  const { slug } = await params;
  const { workspace, user } = await requireWorkspacePage(slug, "admin");
  const db = adminClient();
  const { data: members } = await db.from("workspace_members").select("user_id, role").eq("workspace_id", workspace.id);
  const withEmail = await Promise.all(
    (members ?? []).map(async (m) => {
      const { data } = await db.auth.admin.getUserById(m.user_id);
      return { userId: m.user_id, role: m.role as string, email: data.user?.email ?? "unknown", you: m.user_id === user.id };
    }),
  );
  return (
    <div className="mx-auto max-w-2xl space-y-8">
      <h1 className="h1">Settings</h1>
      <SettingsForm slug={slug} workspace={workspace} />
      <MembersForm slug={slug} members={withEmail} />
    </div>
  );
}
