import { PostCard } from "@/components/post-card";
import { EmptyState } from "@/components/ui";
import { requireWorkspacePage, roleAtLeast } from "@/lib/auth";
import { listPosts } from "@/lib/posts";

export default async function ApprovalsPage({ params }: PageProps<"/w/[slug]/approvals">) {
  const { slug } = await params;
  const access = await requireWorkspacePage(slug);
  const posts = await listPosts(access, { status: ["pending_approval"] });
  const canApprove = roleAtLeast(access.role, "admin");
  return (
    <div className="mx-auto max-w-3xl space-y-4">
      <div>
        <h1 className="h1">Approvals</h1>
        <p className="text-sm text-muted">
          Posts from Claude, the API and editors wait here.{" "}
          {canApprove ? "Approve to schedule them, or request changes." : "An owner or admin approves them."}
        </p>
      </div>
      {posts.length === 0 ? (
        <EmptyState title="All caught up">No posts are waiting for approval.</EmptyState>
      ) : (
        posts.map((p) => <PostCard key={p.id} post={p} slug={slug} timezone={access.workspace.timezone} canApprove={canApprove} />)
      )}
    </div>
  );
}
