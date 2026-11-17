import { z } from "zod";
import { AiUnavailableError, draftPosts } from "@/lib/ai";
import { getSessionUser, workspaceAccess } from "@/lib/auth";
import { PLATFORMS } from "@/lib/types";

export const runtime = "nodejs";
export const maxDuration = 120;

const Body = z.object({
  workspaceId: z.string().uuid(),
  brief: z.string().min(3).max(8000),
  platforms: z.array(z.enum(PLATFORMS)).min(1),
});

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return Response.json({ error: "Sign in first" }, { status: 401 });
  const parsed = Body.safeParse(await request.json().catch(() => null));
  if (!parsed.success) return Response.json({ error: "Write a brief and pick at least one account" }, { status: 400 });
  try {
    const access = await workspaceAccess(user.id, { id: parsed.data.workspaceId }, "editor");
    const drafts = await draftPosts(access.workspace, parsed.data.brief, parsed.data.platforms);
    return Response.json(drafts);
  } catch (err) {
    const status = err instanceof AiUnavailableError ? 503 : 500;
    console.error("AI draft failed", err);
    return Response.json({ error: err instanceof Error ? err.message : "Drafting failed" }, { status });
  }
}
