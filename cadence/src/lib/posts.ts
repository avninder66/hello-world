import "server-only";
import { z } from "zod";
import { roleAtLeast, AccessError, type WorkspaceAccess } from "./auth";
import { validateForPlatform, type ValidationIssue } from "./platforms/limits";
import { adminClient } from "./supabase/admin";
import {
  ACCOUNT_PUBLIC_COLUMNS,
  type MediaItem,
  type Platform,
  type PostSource,
  type PostStatus,
  type PostWithTargets,
  type SocialAccount,
} from "./types";
import { decideStatus, EDITABLE_STATUSES, type Intent } from "./workflow";

export { decideStatus, type Intent } from "./workflow";

export interface Actor extends WorkspaceAccess {
  source: PostSource;
  /** Shown in the activity log, e.g. "Claude (MCP)". */
  label?: string;
}

export const MediaSchema = z.object({
  url: z.string().url(),
  type: z.enum(["image", "video"]).default("image"),
  alt: z.string().max(1500).optional(),
});

export const PostInputSchema = z.object({
  content: z.string().max(63206),
  accountIds: z.array(z.string().uuid()).min(1, "Choose at least one account"),
  overrides: z.record(z.string().uuid(), z.string()).optional(),
  media: z.array(MediaSchema).max(10).default([]),
  scheduledAt: z.string().datetime({ offset: true }).nullable().optional(),
  intent: z.enum(["draft", "schedule", "publish_now"]).default("schedule"),
});
export type PostInput = z.input<typeof PostInputSchema>;

export class PostValidationError extends Error {
  issues: ValidationIssue[];
  constructor(issues: ValidationIssue[]) {
    super(issues.map((i) => i.message).join(" "));
    this.issues = issues;
  }
}

const POST_SELECT =
  "*, post_targets(*, social_accounts(id, platform, display_name, handle))";

async function loadAccounts(workspaceId: string, ids: string[]): Promise<SocialAccount[]> {
  const { data, error } = await adminClient()
    .from("social_accounts")
    .select(ACCOUNT_PUBLIC_COLUMNS)
    .eq("workspace_id", workspaceId)
    .in("id", ids);
  if (error) throw error;
  const accounts = (data ?? []) as unknown as SocialAccount[];
  const missing = ids.filter((id) => !accounts.some((a) => a.id === id));
  if (missing.length) throw new AccessError(`Unknown account id(s) for this workspace: ${missing.join(", ")}`, 400);
  return accounts;
}

function validate(accounts: SocialAccount[], content: string, overrides: Record<string, string>, media: MediaItem[]) {
  const issues: ValidationIssue[] = [];
  for (const a of accounts) {
    issues.push(...validateForPlatform(a.platform, overrides[a.id] ?? content, media));
    if (a.status !== "active") {
      issues.push({ platform: a.platform, message: `${a.display_name} needs reconnecting before it can post.` });
    }
  }
  // De-duplicate when several accounts share a platform.
  return issues.filter((i, idx) => issues.findIndex((j) => j.message === i.message) === idx);
}

async function logEvent(postId: string, actor: Actor | null, action: string, detail: Record<string, unknown> = {}) {
  await adminClient().from("post_events").insert({
    post_id: postId,
    actor_id: actor?.userId ?? null,
    actor_label: actor?.label ?? (actor ? null : "publisher"),
    action,
    detail,
  });
}

export async function getPost(access: WorkspaceAccess, postId: string): Promise<PostWithTargets> {
  const { data, error } = await adminClient()
    .from("posts")
    .select(POST_SELECT)
    .eq("id", postId)
    .eq("workspace_id", access.workspace.id)
    .maybeSingle();
  if (error) throw error;
  if (!data) throw new AccessError("Post not found", 404);
  return data as PostWithTargets;
}

export async function createPost(actor: Actor, raw: PostInput): Promise<{ post: PostWithTargets; warnings: string[] }> {
  if (!roleAtLeast(actor.role, "editor")) throw new AccessError("Viewers can't create posts");
  const input = PostInputSchema.parse(raw);
  const accounts = await loadAccounts(actor.workspace.id, input.accountIds);
  const overrides = input.overrides ?? {};
  const issues = validate(accounts, input.content, overrides, input.media as MediaItem[]);
  if (input.intent !== "draft" && issues.length) throw new PostValidationError(issues);

  const { status, scheduledAt } = decideStatus({
    intent: input.intent as Intent,
    requireApproval: actor.workspace.require_approval,
    role: actor.role,
    source: actor.source,
    scheduledAt: input.scheduledAt ?? null,
  });

  const db = adminClient();
  const { data: post, error } = await db
    .from("posts")
    .insert({
      workspace_id: actor.workspace.id,
      content: input.content,
      media: input.media,
      status,
      scheduled_at: scheduledAt,
      source: actor.source,
      created_by: actor.userId,
      ...(status === "scheduled" ? { approved_by: actor.userId, approved_at: new Date().toISOString() } : {}),
    })
    .select("id")
    .single();
  if (error) throw error;

  const { error: tErr } = await db.from("post_targets").insert(
    accounts.map((a) => ({ post_id: post.id, social_account_id: a.id, content_override: overrides[a.id] ?? null })),
  );
  if (tErr) {
    await db.from("posts").delete().eq("id", post.id);
    throw tErr;
  }
  await logEvent(post.id, actor, "created", { status, scheduled_at: scheduledAt });
  return { post: await getPost(actor, post.id), warnings: issues.map((i) => i.message) };
}

export const PostPatchSchema = PostInputSchema.partial().extend({
  intent: z.enum(["draft", "schedule", "publish_now"]).optional(),
});
export type PostPatch = z.input<typeof PostPatchSchema>;

export async function updatePost(actor: Actor, postId: string, raw: PostPatch) {
  if (!roleAtLeast(actor.role, "editor")) throw new AccessError("Viewers can't edit posts");
  const patch = PostPatchSchema.parse(raw);
  const current = await getPost(actor, postId);
  if (!EDITABLE_STATUSES.includes(current.status)) {
    throw new AccessError(`A ${current.status} post can't be edited`, 409);
  }

  const accountIds = patch.accountIds ?? current.post_targets.map((t) => t.social_account_id);
  const overrides =
    patch.overrides ??
    Object.fromEntries(current.post_targets.filter((t) => t.content_override).map((t) => [t.social_account_id, t.content_override!]));
  const content = patch.content ?? current.content;
  const media = (patch.media ?? current.media) as MediaItem[];
  const scheduledAt = patch.scheduledAt !== undefined ? patch.scheduledAt : current.scheduled_at;
  const intent: Intent = patch.intent ?? (current.status === "draft" ? "draft" : "schedule");

  const accounts = await loadAccounts(actor.workspace.id, accountIds);
  const issues = validate(accounts, content, overrides, media);
  if (intent !== "draft" && issues.length) throw new PostValidationError(issues);

  // Any edit goes back through approval when approval applies to this actor.
  const decided = decideStatus({
    intent,
    requireApproval: actor.workspace.require_approval,
    role: actor.role,
    source: actor.source,
    scheduledAt: scheduledAt ?? null,
  });

  const db = adminClient();
  const { error } = await db
    .from("posts")
    .update({
      content,
      media,
      status: decided.status,
      scheduled_at: decided.scheduledAt,
      review_note: null,
      ...(decided.status === "scheduled"
        ? { approved_by: actor.userId, approved_at: new Date().toISOString() }
        : { approved_by: null, approved_at: null }),
    })
    .eq("id", postId);
  if (error) throw error;

  // Replace targets (none have published yet, given the editable statuses).
  await db.from("post_targets").delete().eq("post_id", postId);
  const { error: tErr } = await db.from("post_targets").insert(
    accounts.map((a) => ({ post_id: postId, social_account_id: a.id, content_override: overrides[a.id] ?? null })),
  );
  if (tErr) throw tErr;
  await logEvent(postId, actor, "updated", { status: decided.status, scheduled_at: decided.scheduledAt });
  return { post: await getPost(actor, postId), warnings: issues.map((i) => i.message) };
}

export async function approvePost(actor: Actor, postId: string) {
  if (!roleAtLeast(actor.role, "admin")) throw new AccessError("Only owners and admins can approve posts");
  if (actor.source === "mcp" || actor.source === "api") {
    throw new AccessError("Posts must be approved by a person in the Cadence app, not through the API");
  }
  const post = await getPost(actor, postId);
  if (post.status !== "pending_approval") throw new AccessError(`Post is ${post.status}, not pending approval`, 409);
  const now = new Date();
  const scheduledAt = !post.scheduled_at || new Date(post.scheduled_at) < now ? now.toISOString() : post.scheduled_at;
  await adminClient()
    .from("posts")
    .update({ status: "scheduled", scheduled_at: scheduledAt, approved_by: actor.userId, approved_at: now.toISOString(), review_note: null })
    .eq("id", postId);
  await logEvent(postId, actor, "approved", { scheduled_at: scheduledAt });
}

export async function rejectPost(actor: Actor, postId: string, note: string) {
  if (!roleAtLeast(actor.role, "admin")) throw new AccessError("Only owners and admins can reject posts");
  const post = await getPost(actor, postId);
  if (!["pending_approval", "scheduled"].includes(post.status)) {
    throw new AccessError(`Post is ${post.status}; it can't be rejected`, 409);
  }
  await adminClient().from("posts").update({ status: "rejected", review_note: note || null }).eq("id", postId);
  await logEvent(postId, actor, "rejected", { note });
}

export async function deletePost(actor: Actor, postId: string) {
  if (!roleAtLeast(actor.role, "editor")) throw new AccessError("Viewers can't delete posts");
  const post = await getPost(actor, postId);
  if (post.status === "publishing") throw new AccessError("Post is publishing right now", 409);
  await adminClient().from("posts").delete().eq("id", postId);
}

/** Re-queues the failed targets of a failed/partially failed post. */
export async function retryPost(actor: Actor, postId: string) {
  if (!roleAtLeast(actor.role, "admin")) throw new AccessError("Only owners and admins can retry posts");
  const post = await getPost(actor, postId);
  if (!["failed", "partially_failed"].includes(post.status)) throw new AccessError("Only failed posts can be retried", 409);
  const db = adminClient();
  await db.from("post_targets").update({ status: "pending", error: null, attempts: 0 }).eq("post_id", postId).eq("status", "failed");
  await db.from("posts").update({ status: "scheduled", scheduled_at: new Date().toISOString() }).eq("id", postId);
  await logEvent(postId, actor, "retried");
}

export interface ListFilters {
  status?: PostStatus[];
  from?: string;
  to?: string;
  limit?: number;
}

export async function listPosts(access: WorkspaceAccess, f: ListFilters = {}): Promise<PostWithTargets[]> {
  let q = adminClient()
    .from("posts")
    .select(POST_SELECT)
    .eq("workspace_id", access.workspace.id)
    .order("scheduled_at", { ascending: true, nullsFirst: false })
    .order("created_at", { ascending: false })
    .limit(Math.min(f.limit ?? 100, 500));
  if (f.status?.length) q = q.in("status", f.status);
  if (f.from) q = q.gte("scheduled_at", f.from);
  if (f.to) q = q.lte("scheduled_at", f.to);
  const { data, error } = await q;
  if (error) throw error;
  return (data ?? []) as PostWithTargets[];
}

export async function listAccounts(access: WorkspaceAccess): Promise<SocialAccount[]> {
  const { data, error } = await adminClient()
    .from("social_accounts")
    .select(ACCOUNT_PUBLIC_COLUMNS)
    .eq("workspace_id", access.workspace.id)
    .order("platform");
  if (error) throw error;
  return (data ?? []) as unknown as SocialAccount[];
}

export async function postEvents(access: WorkspaceAccess, postId: string) {
  await getPost(access, postId);
  const { data } = await adminClient()
    .from("post_events")
    .select("*")
    .eq("post_id", postId)
    .order("created_at", { ascending: false });
  return data ?? [];
}

export type { Platform };
