import "server-only";
import { decryptOptional, decryptSecret, encryptOptional, encryptSecret } from "./crypto";
import { ADAPTERS, PlatformError, type FetchLike, type PublishAccount } from "./platforms";
import { adminClient } from "./supabase/admin";
import type { Post, PostTarget, SocialAccountRow } from "./types";
import { MAX_ATTEMPTS, retryDelayMs, summarise } from "./workflow";

const REFRESH_MARGIN_MS = 10 * 60_000;
// Long-lived tokens (Threads, 60 days) get refreshed when within a week of expiry.
const LONG_LIVED_REFRESH_MS = 7 * 24 * 60 * 60_000;

export interface PublishRunSummary {
  claimed: number;
  requeued: number;
  results: { postId: string; status: string }[];
}

function toPublishAccount(row: SocialAccountRow): PublishAccount {
  if (!row.access_token_enc) throw new PlatformError("Account has no stored credentials", { needsReauth: true });
  return {
    id: row.id,
    platform: row.platform,
    externalId: row.external_id,
    handle: row.handle,
    accessToken: decryptSecret(row.access_token_enc),
    refreshToken: decryptOptional(row.refresh_token_enc),
    tokenExpiresAt: row.token_expires_at ? new Date(row.token_expires_at) : null,
    meta: row.meta ?? {},
  };
}

async function ensureFreshToken(row: SocialAccountRow, account: PublishAccount, fetchImpl: FetchLike) {
  const adapter = ADAPTERS[account.platform];
  if (!adapter.refresh || !account.tokenExpiresAt) return account;
  const margin = account.platform === "threads" ? LONG_LIVED_REFRESH_MS : REFRESH_MARGIN_MS;
  if (account.tokenExpiresAt.getTime() - Date.now() > margin) return account;

  const fresh = await adapter.refresh(account, fetchImpl);
  await adminClient()
    .from("social_accounts")
    .update({
      access_token_enc: encryptSecret(fresh.accessToken),
      refresh_token_enc: fresh.refreshToken !== undefined ? encryptOptional(fresh.refreshToken) : row.refresh_token_enc,
      token_expires_at: fresh.expiresAt?.toISOString() ?? null,
      status: "active",
      last_error: null,
    })
    .eq("id", row.id);
  return { ...account, accessToken: fresh.accessToken, refreshToken: fresh.refreshToken ?? account.refreshToken, tokenExpiresAt: fresh.expiresAt ?? null };
}

/** Publishes one claimed post to all of its not-yet-published targets. */
export async function publishPost(post: Post, fetchImpl: FetchLike = fetch): Promise<string> {
  const db = adminClient();
  const { data, error } = await db
    .from("post_targets")
    .select("*, social_accounts(*)")
    .eq("post_id", post.id);
  if (error) throw error;
  const targets = (data ?? []) as (PostTarget & { social_accounts: SocialAccountRow })[];
  const dryRun = process.env.PUBLISH_DRY_RUN === "true";
  let soonestRetry: number | null = null;

  for (const target of targets) {
    if (target.status === "published") continue;
    const row = target.social_accounts;
    const attempts = target.attempts + 1;
    await db.from("post_targets").update({ status: "publishing", attempts }).eq("id", target.id);

    try {
      let result: { externalId: string; url?: string };
      if (dryRun) {
        result = { externalId: `dry-run-${target.id}`, url: undefined };
      } else {
        let account = toPublishAccount(row);
        account = await ensureFreshToken(row, account, fetchImpl);
        result = await ADAPTERS[row.platform].publish(
          { account, text: target.content_override ?? post.content, media: post.media, idempotencyKey: target.id },
          fetchImpl,
        );
      }
      target.status = "published";
      await db
        .from("post_targets")
        .update({ status: "published", external_id: result.externalId, external_url: result.url ?? null, error: null, published_at: new Date().toISOString() })
        .eq("id", target.id);
    } catch (err) {
      const pe = err instanceof PlatformError ? err : null;
      const message = err instanceof Error ? err.message : String(err);
      const willRetry = !!pe?.retryable && attempts < MAX_ATTEMPTS;
      target.status = willRetry ? "pending" : "failed";
      if (willRetry) {
        const at = Date.now() + retryDelayMs(attempts);
        soonestRetry = soonestRetry === null ? at : Math.min(soonestRetry, at);
      }
      await db.from("post_targets").update({ status: target.status, error: message.slice(0, 1000) }).eq("id", target.id);
      if (pe?.needsReauth) {
        await db.from("social_accounts").update({ status: "needs_reauth", last_error: message.slice(0, 500) }).eq("id", row.id);
      }
    }
  }

  const status = summarise(targets);
  await db
    .from("posts")
    .update({
      status,
      ...(status === "scheduled" && soonestRetry ? { scheduled_at: new Date(soonestRetry).toISOString() } : {}),
      ...(status === "published" || status === "partially_failed" ? { published_at: new Date().toISOString() } : {}),
    })
    .eq("id", post.id);
  await db.from("post_events").insert({
    post_id: post.id,
    actor_label: dryRun ? "publisher (dry run)" : "publisher",
    action: status === "scheduled" ? "retry_scheduled" : status,
    detail: { targets: targets.map((t) => ({ id: t.id, status: t.status })) },
  });
  return status;
}

/** Claims due posts and publishes them. Called by the cron endpoint. */
export async function runPublisher(limit = 20, fetchImpl: FetchLike = fetch): Promise<PublishRunSummary> {
  const db = adminClient();
  const { data: requeued } = await db.rpc("requeue_stale_posts", { p_minutes: 15 });
  const { data, error } = await db.rpc("claim_due_posts", { p_limit: limit });
  if (error) throw error;
  const posts = (data ?? []) as Post[];
  const results: PublishRunSummary["results"] = [];
  for (const post of posts) {
    try {
      results.push({ postId: post.id, status: await publishPost(post, fetchImpl) });
    } catch (err) {
      console.error("publishPost crashed", post.id, err);
      await db.from("posts").update({ status: "failed" }).eq("id", post.id);
      results.push({ postId: post.id, status: "failed" });
    }
  }
  return { claimed: posts.length, requeued: (requeued as number) ?? 0, results };
}
