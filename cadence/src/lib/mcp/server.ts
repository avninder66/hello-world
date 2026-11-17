import "server-only";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { apiWorkspaceAccess, listUserWorkspaces, type ApiKeyAuth } from "../auth";
import { appUrl } from "../env";
import { PLATFORM_SPECS, countChars } from "../platforms/limits";
import {
  createPost,
  deletePost,
  getPost,
  listAccounts,
  listPosts,
  PostValidationError,
  updatePost,
  type Actor,
} from "../posts";
import { POST_STATUSES, type PostWithTargets } from "../types";

const json = (value: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(value, null, 2) }] });
const fail = (message: string) => ({ content: [{ type: "text" as const, text: message }], isError: true });

function summarisePost(p: PostWithTargets, slug: string) {
  return {
    id: p.id,
    status: p.status,
    scheduled_at: p.scheduled_at,
    content: p.content,
    media: p.media,
    review_note: p.review_note,
    targets: p.post_targets.map((t) => ({
      account_id: t.social_account_id,
      platform: t.social_accounts?.platform,
      account: t.social_accounts?.display_name,
      content_override: t.content_override,
      status: t.status,
      url: t.external_url,
      error: t.error,
    })),
    review_url: `${appUrl()}/w/${slug}/compose?post=${p.id}`,
  };
}

async function actorFor(auth: ApiKeyAuth, workspaceId: string, minRole: "viewer" | "editor" = "viewer"): Promise<Actor> {
  const access = await apiWorkspaceAccess(auth, workspaceId, minRole);
  return { ...access, source: "mcp", label: "Claude (MCP)" };
}

async function guard<T>(fn: () => Promise<T>) {
  try {
    return await fn();
  } catch (err) {
    if (err instanceof PostValidationError) {
      return fail(`The post doesn't fit these platforms yet:\n- ${err.issues.map((i) => i.message).join("\n- ")}`);
    }
    if (err instanceof z.ZodError) return fail(`Invalid input: ${err.issues.map((i) => `${i.path.join(".")}: ${i.message}`).join("; ")}`);
    return fail(err instanceof Error ? err.message : String(err));
  }
}

const Media = z.object({
  url: z.string().url().describe("Public https URL of the image or video"),
  type: z.enum(["image", "video"]).default("image"),
  alt: z.string().optional().describe("Alt text describing the image"),
});

/** Builds an MCP server whose tools act as the API key's owner. */
export function buildMcpServer(auth: ApiKeyAuth): McpServer {
  const server = new McpServer(
    { name: "cadence", version: "0.1.0" },
    {
      instructions: [
        "Cadence schedules social media posts for one or more client workspaces.",
        "Start with list_workspaces, then get_workspace for the brand voice, timezone and connected accounts before writing.",
        "Write a version tailored to each platform (use overrides for per-account wording) and check lengths with check_post.",
        "Posts you create usually land in 'pending_approval': a person approves them in the Cadence app before anything is published. Share the review_url with the user.",
        "Times are ISO 8601 with an offset; interpret the user's times in the workspace timezone.",
      ].join(" "),
    },
  );

  server.registerTool(
    "list_workspaces",
    {
      title: "List workspaces",
      description: "Lists the client/brand workspaces this key can use, with your role and whether posts need approval.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () =>
      guard(async () => {
        const all = await listUserWorkspaces(auth.userId);
        const visible = auth.workspaceId ? all.filter((w) => w.id === auth.workspaceId) : all;
        return json(visible.map((w) => ({ id: w.id, name: w.name, slug: w.slug, timezone: w.timezone, role: w.role, require_approval: w.require_approval })));
      }),
  );

  server.registerTool(
    "get_workspace",
    {
      title: "Get workspace details",
      description: "Brand voice, timezone, connected social accounts (ids to post to) and per-platform rules for a workspace.",
      inputSchema: { workspace_id: z.string().uuid() },
      annotations: { readOnlyHint: true },
    },
    async ({ workspace_id }) =>
      guard(async () => {
        const actor = await actorFor(auth, workspace_id);
        const accounts = await listAccounts(actor);
        const platforms = [...new Set(accounts.map((a) => a.platform))];
        return json({
          workspace: {
            id: actor.workspace.id, name: actor.workspace.name, timezone: actor.workspace.timezone,
            require_approval: actor.workspace.require_approval, brand_voice: actor.workspace.brand_voice || "(none provided)",
          },
          your_role: actor.role,
          accounts: accounts.map((a) => ({ id: a.id, platform: a.platform, name: a.display_name, handle: a.handle, status: a.status })),
          platform_rules: platforms.map((p) => ({ platform: p, max_chars: PLATFORM_SPECS[p].maxChars, max_images: PLATFORM_SPECS[p].maxImages, requires_media: PLATFORM_SPECS[p].requiresMedia, notes: PLATFORM_SPECS[p].notes })),
          now: new Date().toISOString(),
        });
      }),
  );

  server.registerTool(
    "check_post",
    {
      title: "Check post length",
      description: "Counts characters the way each platform does, without saving anything.",
      inputSchema: { text: z.string(), platforms: z.array(z.enum(["linkedin", "x", "facebook", "instagram", "threads", "bluesky", "mastodon"])).min(1) },
      annotations: { readOnlyHint: true },
    },
    async ({ text, platforms }) =>
      json(platforms.map((p) => {
        const count = countChars(p, text);
        return { platform: p, count, max: PLATFORM_SPECS[p].maxChars, fits: count <= PLATFORM_SPECS[p].maxChars };
      })),
  );

  server.registerTool(
    "list_posts",
    {
      title: "List posts",
      description: "Lists posts in a workspace, optionally filtered by status and a scheduled time window. Use it to see the calendar and avoid clashes.",
      inputSchema: {
        workspace_id: z.string().uuid(),
        status: z.array(z.enum(POST_STATUSES)).optional(),
        from: z.string().datetime({ offset: true }).optional(),
        to: z.string().datetime({ offset: true }).optional(),
        limit: z.number().int().min(1).max(200).default(50),
      },
      annotations: { readOnlyHint: true },
    },
    async ({ workspace_id, status, from, to, limit }) =>
      guard(async () => {
        const actor = await actorFor(auth, workspace_id);
        const posts = await listPosts(actor, { status, from, to, limit });
        return json(posts.map((p) => summarisePost(p, actor.workspace.slug)));
      }),
  );

  server.registerTool(
    "get_post",
    {
      title: "Get post",
      description: "Full details of one post, including per-account status, links and errors.",
      inputSchema: { workspace_id: z.string().uuid(), post_id: z.string().uuid() },
      annotations: { readOnlyHint: true },
    },
    async ({ workspace_id, post_id }) =>
      guard(async () => {
        const actor = await actorFor(auth, workspace_id);
        return json(summarisePost(await getPost(actor, post_id), actor.workspace.slug));
      }),
  );

  const postFields = {
    content: z.string().describe("Main post text, used for every account without an override"),
    account_ids: z.array(z.string().uuid()).min(1).describe("Social account ids from get_workspace"),
    overrides: z.record(z.string().uuid(), z.string()).optional().describe("Per-account text keyed by account id, e.g. a shorter version for X"),
    media: z.array(Media).max(10).optional(),
    scheduled_at: z.string().datetime({ offset: true }).optional().describe("When to publish (ISO 8601 with offset). Omit to publish as soon as approved."),
    as_draft: z.boolean().default(false).describe("Save as a draft that won't publish or enter approval"),
  };

  server.registerTool(
    "create_post",
    {
      title: "Create & schedule post",
      description:
        "Creates a post for one or more accounts. It's validated against each platform's limits. If the workspace requires approval it waits for a person to approve it in Cadence; otherwise it is scheduled.",
      inputSchema: { workspace_id: z.string().uuid(), ...postFields },
    },
    async ({ workspace_id, content, account_ids, overrides, media, scheduled_at, as_draft }) =>
      guard(async () => {
        const actor = await actorFor(auth, workspace_id, "editor");
        const { post, warnings } = await createPost(actor, {
          content, accountIds: account_ids, overrides, media: media ?? [], scheduledAt: scheduled_at ?? null,
          intent: as_draft ? "draft" : "schedule",
        });
        return json({ ...summarisePost(post, actor.workspace.slug), warnings });
      }),
  );

  server.registerTool(
    "update_post",
    {
      title: "Update post",
      description: "Edits a post that hasn't published yet. Edited posts go back for approval when the workspace requires it.",
      inputSchema: {
        workspace_id: z.string().uuid(),
        post_id: z.string().uuid(),
        ...Object.fromEntries(Object.entries(postFields).map(([k, v]) => [k, v.optional()])),
      } as Record<string, z.ZodTypeAny>,
    },
    async (args: Record<string, unknown>) =>
      guard(async () => {
        const a = args as {
          workspace_id: string; post_id: string; content?: string; account_ids?: string[]; overrides?: Record<string, string>;
          media?: z.infer<typeof Media>[]; scheduled_at?: string; as_draft?: boolean;
        };
        const actor = await actorFor(auth, a.workspace_id, "editor");
        const { post, warnings } = await updatePost(actor, a.post_id, {
          content: a.content, accountIds: a.account_ids, overrides: a.overrides, media: a.media, scheduledAt: a.scheduled_at,
          intent: a.as_draft === undefined ? undefined : a.as_draft ? "draft" : "schedule",
        });
        return json({ ...summarisePost(post, actor.workspace.slug), warnings });
      }),
  );

  server.registerTool(
    "delete_post",
    {
      title: "Delete post",
      description: "Deletes a post that is not currently publishing. Does not remove anything already published on the platform.",
      inputSchema: { workspace_id: z.string().uuid(), post_id: z.string().uuid() },
      annotations: { destructiveHint: true },
    },
    async ({ workspace_id, post_id }) =>
      guard(async () => {
        const actor = await actorFor(auth, workspace_id, "editor");
        await deletePost(actor, post_id);
        return json({ deleted: post_id });
      }),
  );

  return server;
}
