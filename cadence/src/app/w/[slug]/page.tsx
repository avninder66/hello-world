import Link from "next/link";
import { PostCard } from "@/components/post-card";
import { EmptyState } from "@/components/ui";
import { requireWorkspacePage, roleAtLeast } from "@/lib/auth";
import { listAccounts, listPosts } from "@/lib/posts";

export default async function QueuePage({ params }: PageProps<"/w/[slug]">) {
  const { slug } = await params;
  const access = await requireWorkspacePage(slug);
  const [upcoming, drafts, recent, accounts] = await Promise.all([
    listPosts(access, { status: ["pending_approval", "scheduled", "publishing"], limit: 100 }),
    listPosts(access, { status: ["draft", "rejected"], limit: 50 }),
    listPosts(access, { status: ["published", "partially_failed", "failed"], limit: 20 }),
    listAccounts(access),
  ]);
  const canApprove = roleAtLeast(access.role, "admin");
  const tz = access.workspace.timezone;
  recent.sort((a, b) => (b.published_at ?? b.updated_at).localeCompare(a.published_at ?? a.updated_at));

  return (
    <div className="mx-auto max-w-3xl space-y-8">
      <div>
        <h1 className="h1">{access.workspace.name}</h1>
        <p className="text-sm text-muted">Times shown in {tz.replaceAll("_", " ")}.</p>
      </div>

      {accounts.length === 0 && (
        <EmptyState title="Connect a social account to get started">
          <Link href={`/w/${slug}/accounts`} className="btn btn-primary mt-2">Connect accounts</Link>
        </EmptyState>
      )}

      <section className="space-y-3">
        <h2 className="h2">Upcoming</h2>
        {upcoming.length === 0 ? (
          <EmptyState title="Nothing scheduled">Write a post, or ask Claude to plan next week&apos;s content.</EmptyState>
        ) : (
          upcoming.map((p) => <PostCard key={p.id} post={p} slug={slug} timezone={tz} canApprove={canApprove} />)
        )}
      </section>

      {drafts.length > 0 && (
        <section className="space-y-3">
          <h2 className="h2">Drafts &amp; changes requested</h2>
          {drafts.map((p) => <PostCard key={p.id} post={p} slug={slug} timezone={tz} canApprove={canApprove} />)}
        </section>
      )}

      {recent.length > 0 && (
        <section className="space-y-3">
          <h2 className="h2">Recently published</h2>
          {recent.map((p) => <PostCard key={p.id} post={p} slug={slug} timezone={tz} canApprove={canApprove} />)}
        </section>
      )}
    </div>
  );
}
