"use client";

import Link from "next/link";
import { useState, useTransition } from "react";
import { postAction } from "@/app/actions";
import { PLATFORM_SPECS } from "@/lib/platforms/limits";
import type { PostWithTargets } from "@/lib/types";
import { formatWhen, PlatformIcon, StatusBadge } from "./ui";

const SOURCE_LABEL = { web: null, ai: "Drafted with Claude", api: "Via API", mcp: "Written by Claude" } as const;

export function PostCard({
  post, slug, timezone, canApprove,
}: { post: PostWithTargets; slug: string; timezone: string; canApprove: boolean }) {
  const [pending, start] = useTransition();
  const [error, setError] = useState<string | null>(null);
  const [rejecting, setRejecting] = useState(false);
  const [note, setNote] = useState("");

  const run = (action: "approve" | "reject" | "delete" | "retry") =>
    start(async () => {
      if (action === "delete" && !confirm("Delete this post? Anything already published stays on the platform.")) return;
      const res = await postAction(slug, post.id, action, note);
      setError(res.ok ? null : res.error);
      if (res.ok) setRejecting(false);
    });

  const editable = ["draft", "pending_approval", "scheduled", "rejected"].includes(post.status);
  const source = SOURCE_LABEL[post.source];

  return (
    <article className={`card p-4 ${pending ? "opacity-60" : ""}`}>
      <header className="mb-2 flex flex-wrap items-center gap-2 text-sm">
        <StatusBadge status={post.status} />
        <span className="text-muted">{formatWhen(post.scheduled_at, timezone)}</span>
        {source && <span className="rounded-full bg-accent-soft px-2 py-0.5 text-xs text-accent">{source}</span>}
        <span className="ml-auto flex gap-1">
          {post.post_targets.map((t) =>
            t.social_accounts ? (
              <a key={t.id} href={t.external_url ?? undefined} target="_blank" rel="noreferrer" title={`${t.social_accounts.display_name}: ${t.status}${t.error ? ` — ${t.error}` : ""}`}
                className={t.status === "failed" ? "rounded-md ring-2 ring-bad" : ""}>
                <PlatformIcon platform={t.social_accounts.platform} />
              </a>
            ) : null,
          )}
        </span>
      </header>
      <p className="line-clamp-4 whitespace-pre-wrap text-sm">{post.content || <em className="text-muted">No main text</em>}</p>
      {post.media.length > 0 && (
        <div className="mt-2 flex gap-2">
          {post.media.slice(0, 4).map((m) =>
            m.type === "image" ? (
              // eslint-disable-next-line @next/next/no-img-element
              <img key={m.url} src={m.url} alt={m.alt ?? ""} className="h-14 w-14 rounded-md border border-line object-cover" />
            ) : (
              <span key={m.url} className="flex h-14 w-14 items-center justify-center rounded-md border border-line text-xs text-muted">Video</span>
            ),
          )}
        </div>
      )}
      {post.post_targets.some((t) => t.content_override) && (
        <p className="hint mt-2">Custom wording for {post.post_targets.filter((t) => t.content_override && t.social_accounts).map((t) => `${PLATFORM_SPECS[t.social_accounts!.platform].label} (${t.social_accounts!.display_name})`).join(", ")}</p>
      )}
      {post.review_note && <p className="mt-2 rounded-lg bg-bad-soft px-3 py-2 text-sm text-bad">Reviewer: {post.review_note}</p>}
      {post.post_targets.filter((t) => t.error).map((t) => (
        <p key={t.id} className="mt-2 text-xs text-bad">{t.social_accounts?.display_name}: {t.error}</p>
      ))}

      {rejecting && (
        <div className="mt-3 flex gap-2">
          <input className="input" placeholder="What should change? (shown to the author and Claude)" value={note} onChange={(e) => setNote(e.target.value)} />
          <button className="btn btn-danger" onClick={() => run("reject")} disabled={pending}>Reject</button>
        </div>
      )}

      <footer className="mt-3 flex flex-wrap gap-2">
        {canApprove && post.status === "pending_approval" && (
          <>
            <button className="btn btn-primary btn-sm" onClick={() => run("approve")} disabled={pending}>Approve</button>
            {!rejecting && <button className="btn btn-sm" onClick={() => setRejecting(true)}>Request changes</button>}
          </>
        )}
        {canApprove && ["failed", "partially_failed"].includes(post.status) && (
          <button className="btn btn-sm" onClick={() => run("retry")} disabled={pending}>Retry failed</button>
        )}
        {editable && <Link href={`/w/${slug}/compose?post=${post.id}`} className="btn btn-sm">Edit</Link>}
        {post.status !== "publishing" && (
          <button className="btn btn-sm btn-danger ml-auto" onClick={() => run("delete")} disabled={pending}>Delete</button>
        )}
      </footer>
      {error && <p className="mt-2 text-sm text-bad">{error}</p>}
    </article>
  );
}
