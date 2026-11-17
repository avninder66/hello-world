import { listUserWorkspaces } from "@/lib/auth";
import { apiHandler } from "@/lib/api";

export const runtime = "nodejs";

export const GET = apiHandler(async (_req, auth) => {
  const all = await listUserWorkspaces(auth.userId);
  return { workspaces: auth.workspaceId ? all.filter((w) => w.id === auth.workspaceId) : all };
});
