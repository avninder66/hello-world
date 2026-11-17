import { apiHandler } from "@/lib/api";
import { apiWorkspaceAccess } from "@/lib/auth";
import { listAccounts } from "@/lib/posts";

export const runtime = "nodejs";

export const GET = apiHandler<RouteContext<"/api/v1/workspaces/[workspaceId]/accounts">>(async (_req, auth, ctx) => {
  const { workspaceId } = await ctx.params;
  return { accounts: await listAccounts(await apiWorkspaceAccess(auth, workspaceId)) };
});
