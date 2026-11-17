import { requireWorkspacePage, roleAtLeast } from "@/lib/auth";
import { getPost, listAccounts } from "@/lib/posts";
import { isoToZonedLocal } from "@/lib/time";
import { Composer, type ComposerInitial } from "./composer";

export default async function ComposePage({ params, searchParams }: PageProps<"/w/[slug]/compose">) {
  const { slug } = await params;
  const sp = await searchParams;
  const access = await requireWorkspacePage(slug, "editor");
  const tz = access.workspace.timezone;
  const accounts = (await listAccounts(access)).filter((a) => a.status !== "disabled");

  let initial: ComposerInitial | null = null;
  const postId = typeof sp.post === "string" ? sp.post : null;
  if (postId) {
    const post = await getPost(access, postId).catch(() => null);
    if (post) {
      initial = {
        id: post.id,
        status: post.status,
        content: post.content,
        media: post.media,
        accountIds: post.post_targets.map((t) => t.social_account_id),
        overrides: Object.fromEntries(post.post_targets.filter((t) => t.content_override).map((t) => [t.social_account_id, t.content_override!])),
        scheduledLocal: post.scheduled_at ? isoToZonedLocal(post.scheduled_at, tz) : "",
        reviewNote: post.review_note,
        source: post.source,
      };
    }
  }
  const date = typeof sp.date === "string" && /^\d{4}-\d{2}-\d{2}$/.test(sp.date) ? sp.date : null;

  const trusted = roleAtLeast(access.role, "admin");
  return (
    <Composer
      slug={slug}
      workspaceId={access.workspace.id}
      timezone={tz}
      accounts={accounts}
      initial={initial}
      defaultLocal={date ? `${date}T09:00` : ""}
      needsApproval={access.workspace.require_approval && !trusted}
      aiEnabled={!!process.env.ANTHROPIC_API_KEY}
    />
  );
}
