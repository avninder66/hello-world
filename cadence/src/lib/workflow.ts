import type { PostSource, PostStatus, TargetStatus, WorkspaceRole } from "./types";

export type Intent = "draft" | "schedule" | "publish_now";

export const EDITABLE_STATUSES: PostStatus[] = ["draft", "pending_approval", "scheduled", "rejected"];

/**
 * Decides where a new or edited post lands.
 *
 * Approval is required (when the workspace has it switched on) for anything
 * written by Claude or the API, and for posts by editors. Owners/admins
 * working in the web app schedule directly.
 */
export function decideStatus(p: {
  intent: Intent;
  requireApproval: boolean;
  role: WorkspaceRole;
  source: PostSource;
  scheduledAt: string | null;
  now?: Date;
}): { status: PostStatus; scheduledAt: string | null } {
  const now = p.now ?? new Date();
  if (p.intent === "draft") return { status: "draft", scheduledAt: p.scheduledAt };

  const when =
    p.intent === "publish_now" || !p.scheduledAt || new Date(p.scheduledAt) < now
      ? now.toISOString()
      : new Date(p.scheduledAt).toISOString();

  const automated = p.source === "mcp" || p.source === "api";
  const trusted = (p.role === "owner" || p.role === "admin") && !automated;
  if (p.requireApproval && !trusted) {
    // Keep the requested time; approval publishes immediately if it has passed.
    return { status: "pending_approval", scheduledAt: p.intent === "publish_now" ? null : when };
  }
  return { status: "scheduled", scheduledAt: when };
}

/** Overall post status from its targets once a publishing run finishes. */
export function summarise(targets: { status: TargetStatus }[]): PostStatus {
  const published = targets.filter((t) => t.status === "published").length;
  if (published === targets.length) return "published";
  if (targets.some((t) => t.status === "pending")) return "scheduled"; // retry pending
  return published > 0 ? "partially_failed" : "failed";
}

export const MAX_ATTEMPTS = 3;

/** Exponential backoff for retryable publish failures: 2, 4, 8... minutes. */
export function retryDelayMs(attempt: number): number {
  return Math.min(2 ** attempt, 60) * 60_000;
}
