export const PLATFORMS = [
  "linkedin",
  "x",
  "facebook",
  "instagram",
  "threads",
  "bluesky",
  "mastodon",
] as const;
export type Platform = (typeof PLATFORMS)[number];

export type WorkspaceRole = "owner" | "admin" | "editor" | "viewer";

export const POST_STATUSES = [
  "draft",
  "pending_approval",
  "scheduled",
  "publishing",
  "published",
  "partially_failed",
  "failed",
  "rejected",
] as const;
export type PostStatus = (typeof POST_STATUSES)[number];
export type TargetStatus = "pending" | "publishing" | "published" | "failed";
export type PostSource = "web" | "ai" | "api" | "mcp";

export interface MediaItem {
  url: string;
  type: "image" | "video";
  alt?: string;
}

export interface Workspace {
  id: string;
  name: string;
  slug: string;
  timezone: string;
  require_approval: boolean;
  brand_voice: string;
  created_at: string;
}

/** Social account as visible to clients (no tokens). */
export interface SocialAccount {
  id: string;
  workspace_id: string;
  platform: Platform;
  external_id: string;
  display_name: string;
  handle: string | null;
  avatar_url: string | null;
  token_expires_at: string | null;
  meta: Record<string, unknown>;
  status: "active" | "needs_reauth" | "disabled";
  last_error: string | null;
  created_at: string;
}

/** Server-only row including encrypted tokens. */
export interface SocialAccountRow extends SocialAccount {
  access_token_enc: string | null;
  refresh_token_enc: string | null;
}

export interface Post {
  id: string;
  workspace_id: string;
  content: string;
  media: MediaItem[];
  status: PostStatus;
  scheduled_at: string | null;
  source: PostSource;
  created_by: string | null;
  approved_by: string | null;
  approved_at: string | null;
  review_note: string | null;
  published_at: string | null;
  created_at: string;
  updated_at: string;
}

export interface PostTarget {
  id: string;
  post_id: string;
  social_account_id: string;
  content_override: string | null;
  status: TargetStatus;
  external_id: string | null;
  external_url: string | null;
  error: string | null;
  attempts: number;
  published_at: string | null;
}

export interface PostWithTargets extends Post {
  post_targets: (PostTarget & {
    social_accounts: Pick<SocialAccount, "id" | "platform" | "display_name" | "handle"> | null;
  })[];
}

/** Columns that are safe to select for social accounts. */
export const ACCOUNT_PUBLIC_COLUMNS =
  "id, workspace_id, platform, external_id, display_name, handle, avatar_url, token_expires_at, meta, status, last_error, created_at";
