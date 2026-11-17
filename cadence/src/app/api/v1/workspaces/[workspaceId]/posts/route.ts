import { apiHandler } from "@/lib/api";
import { apiWorkspaceAccess } from "@/lib/auth";
import { createPost, listPosts } from "@/lib/posts";
import { POST_STATUSES, type PostStatus } from "@/lib/types";

export const runtime = "nodejs";

type Ctx = RouteContext<"/api/v1/workspaces/[workspaceId]/posts">;

/** GET ?status=scheduled,pending_approval&from=ISO&to=ISO */
export const GET = apiHandler<Ctx>(async (req, auth, ctx) => {
  const { workspaceId } = await ctx.params;
  const url = new URL(req.url);
  const status = url.searchParams.get("status")?.split(",").filter((s): s is PostStatus => POST_STATUSES.includes(s as PostStatus));
  const access = await apiWorkspaceAccess(auth, workspaceId);
  return {
    posts: await listPosts(access, {
      status,
      from: url.searchParams.get("from") ?? undefined,
      to: url.searchParams.get("to") ?? undefined,
    }),
  };
});

/** POST { content, accountIds, overrides?, media?, scheduledAt?, intent? } */
export const POST = apiHandler<Ctx>(async (req, auth, ctx) => {
  const { workspaceId } = await ctx.params;
  const access = await apiWorkspaceAccess(auth, workspaceId, "editor");
  const result = await createPost({ ...access, source: "api", label: "API" }, await req.json());
  return Response.json(result, { status: 201 });
});
