import "server-only";
import { redirect } from "next/navigation";
import { hashApiKey, API_KEY_PREFIX } from "./crypto";
import { adminClient } from "./supabase/admin";
import { userClient } from "./supabase/server";
import type { Workspace, WorkspaceRole } from "./types";

const RANK: Record<WorkspaceRole, number> = { viewer: 0, editor: 1, admin: 2, owner: 3 };

export function roleAtLeast(role: WorkspaceRole, min: WorkspaceRole): boolean {
  return RANK[role] >= RANK[min];
}

export class AccessError extends Error {
  status: number;
  constructor(message: string, status = 403) {
    super(message);
    this.status = status;
  }
}

export async function getSessionUser() {
  const supabase = await userClient();
  const { data } = await supabase.auth.getUser();
  return data.user;
}

export async function requireUser() {
  const user = await getSessionUser();
  if (!user) redirect("/login");
  return user;
}

export interface WorkspaceAccess {
  userId: string;
  workspace: Workspace;
  role: WorkspaceRole;
}

/** Loads a workspace by id or slug and checks the user's role in it. */
export async function workspaceAccess(
  userId: string,
  ref: { id?: string; slug?: string },
  minRole: WorkspaceRole = "viewer",
): Promise<WorkspaceAccess> {
  const db = adminClient();
  let q = db.from("workspaces").select("*, workspace_members!inner(role, user_id)").eq("workspace_members.user_id", userId);
  q = ref.id ? q.eq("id", ref.id) : q.eq("slug", ref.slug ?? "");
  const { data } = await q.maybeSingle();
  if (!data) throw new AccessError("Workspace not found or you are not a member", 404);
  const { workspace_members, ...workspace } = data as Workspace & { workspace_members: { role: WorkspaceRole }[] };
  const role = workspace_members[0].role;
  if (!roleAtLeast(role, minRole)) throw new AccessError(`This needs the ${minRole} role (you are ${role})`);
  return { userId, workspace, role };
}

/** For pages: signed-in user + workspace by slug, redirecting when not allowed. */
export async function requireWorkspacePage(slug: string, minRole: WorkspaceRole = "viewer") {
  const user = await requireUser();
  try {
    return { user, ...(await workspaceAccess(user.id, { slug }, minRole)) };
  } catch {
    redirect("/");
  }
}

export async function listUserWorkspaces(userId: string) {
  const { data } = await adminClient()
    .from("workspace_members")
    .select("role, workspaces(*)")
    .eq("user_id", userId)
    .order("created_at");
  return ((data ?? []) as unknown as { role: WorkspaceRole; workspaces: Workspace }[]).map((r) => ({
    ...r.workspaces,
    role: r.role,
  }));
}

export interface ApiKeyAuth {
  userId: string;
  keyId: string;
  /** When set, the key may only touch this workspace. */
  workspaceId: string | null;
}

/** Validates "Authorization: Bearer cad_..." on API/MCP requests. */
export async function authenticateApiKey(request: Request): Promise<ApiKeyAuth | null> {
  const header = request.headers.get("authorization") ?? "";
  const key = header.replace(/^Bearer\s+/i, "").trim();
  if (!key.startsWith(API_KEY_PREFIX)) return null;
  const db = adminClient();
  const { data } = await db
    .from("api_keys")
    .select("id, user_id, workspace_id, revoked_at")
    .eq("key_hash", hashApiKey(key))
    .maybeSingle();
  if (!data || data.revoked_at) return null;
  void db.from("api_keys").update({ last_used_at: new Date().toISOString() }).eq("id", data.id).then(() => {});
  return { userId: data.user_id, keyId: data.id, workspaceId: data.workspace_id };
}

/** Workspace access for an API key, honouring a key's workspace restriction. */
export async function apiWorkspaceAccess(auth: ApiKeyAuth, workspaceId: string, minRole: WorkspaceRole = "viewer") {
  if (auth.workspaceId && auth.workspaceId !== workspaceId) {
    throw new AccessError("This API key is restricted to a different workspace");
  }
  return workspaceAccess(auth.userId, { id: workspaceId }, minRole);
}
