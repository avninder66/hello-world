"use server";

import { revalidatePath } from "next/cache";
import { redirect } from "next/navigation";
import { z } from "zod";
import { AccessError, requireUser, roleAtLeast, workspaceAccess } from "@/lib/auth";
import { encryptSecret, generateApiKey } from "@/lib/crypto";
import { appUrl } from "@/lib/env";
import { createBskySession, DEFAULT_BSKY_SERVICE } from "@/lib/platforms/bluesky";
import { normaliseInstance, verifyMastodon } from "@/lib/platforms/mastodon";
import {
  approvePost,
  createPost,
  deletePost,
  PostValidationError,
  rejectPost,
  retryPost,
  updatePost,
  type Actor,
  type PostInput,
} from "@/lib/posts";
import { adminClient } from "@/lib/supabase/admin";
import { userClient } from "@/lib/supabase/server";
import type { WorkspaceRole } from "@/lib/types";

export type ActionResult<T = object> = ({ ok: true } & T) | { ok: false; error: string; issues?: string[] };

async function actor(slug: string, minRole: WorkspaceRole = "viewer"): Promise<Actor> {
  const user = await requireUser();
  const access = await workspaceAccess(user.id, { slug }, minRole);
  return { ...access, source: "web", label: user.email ?? undefined };
}

function failure(err: unknown): { ok: false; error: string; issues?: string[] } {
  if (err instanceof PostValidationError) return { ok: false, error: "Fix these before scheduling:", issues: err.issues.map((i) => i.message) };
  if (err instanceof z.ZodError) return { ok: false, error: err.issues.map((i) => i.message).join(" ") };
  if (err instanceof AccessError) return { ok: false, error: err.message };
  console.error(err);
  return { ok: false, error: err instanceof Error ? err.message : "Something went wrong" };
}

export async function signOut() {
  const supabase = await userClient();
  await supabase.auth.signOut();
  redirect("/login");
}

// ---------------------------------------------------------------- workspaces

export async function createWorkspace(_prev: unknown, form: FormData): Promise<ActionResult> {
  await requireUser();
  const name = String(form.get("name") ?? "").trim();
  const timezone = String(form.get("timezone") ?? "Europe/London");
  const slug =
    String(form.get("slug") ?? "").trim().toLowerCase() ||
    name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 40);
  if (!name) return { ok: false, error: "Give the workspace a name" };
  const supabase = await userClient();
  const { data, error } = await supabase.rpc("create_workspace", { p_name: name, p_slug: slug, p_timezone: timezone });
  if (error) {
    return { ok: false, error: error.code === "23505" ? "That URL name is taken; pick another" : error.message };
  }
  redirect(`/w/${(data as { slug: string }).slug}/accounts`);
}

export async function updateWorkspace(slug: string, _prev: unknown, form: FormData): Promise<ActionResult> {
  try {
    const a = await actor(slug, "admin");
    const { error } = await adminClient()
      .from("workspaces")
      .update({
        name: String(form.get("name") ?? a.workspace.name).trim() || a.workspace.name,
        timezone: String(form.get("timezone") ?? a.workspace.timezone),
        brand_voice: String(form.get("brand_voice") ?? ""),
        require_approval: form.get("require_approval") === "on",
      })
      .eq("id", a.workspace.id);
    if (error) throw error;
    revalidatePath(`/w/${slug}`, "layout");
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

export async function addMember(slug: string, _prev: unknown, form: FormData): Promise<ActionResult> {
  try {
    const a = await actor(slug, "admin");
    const email = String(form.get("email") ?? "").trim();
    const role = z.enum(["admin", "editor", "viewer"]).parse(form.get("role"));
    const db = adminClient();
    let { data: userId } = await db.rpc("find_user_id_by_email", { p_email: email });
    if (!userId) {
      const invited = await db.auth.admin.inviteUserByEmail(email, { redirectTo: `${appUrl()}/auth/callback` });
      if (invited.error) throw invited.error;
      userId = invited.data.user.id;
    }
    const { error } = await db.from("workspace_members").upsert({ workspace_id: a.workspace.id, user_id: userId, role });
    if (error) throw error;
    revalidatePath(`/w/${slug}/settings`);
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

export async function removeMember(slug: string, userId: string): Promise<ActionResult> {
  try {
    const a = await actor(slug, "admin");
    const { data: target } = await adminClient()
      .from("workspace_members").select("role").eq("workspace_id", a.workspace.id).eq("user_id", userId).single();
    if (target?.role === "owner") throw new AccessError("The owner can't be removed");
    await adminClient().from("workspace_members").delete().eq("workspace_id", a.workspace.id).eq("user_id", userId);
    revalidatePath(`/w/${slug}/settings`);
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

// --------------------------------------------------------------------- posts

export async function savePost(slug: string, postId: string | null, input: PostInput): Promise<ActionResult<{ postId: string; status: string; warnings: string[] }>> {
  try {
    const a = await actor(slug, "editor");
    const { post, warnings } = postId ? await updatePost(a, postId, input) : await createPost(a, input);
    revalidatePath(`/w/${slug}`, "layout");
    return { ok: true, postId: post.id, status: post.status, warnings };
  } catch (err) {
    return failure(err);
  }
}

export async function postAction(
  slug: string,
  postId: string,
  action: "approve" | "reject" | "delete" | "retry",
  note = "",
): Promise<ActionResult> {
  try {
    const a = await actor(slug, action === "delete" ? "editor" : "admin");
    if (action === "approve") await approvePost(a, postId);
    if (action === "reject") await rejectPost(a, postId, note);
    if (action === "delete") await deletePost(a, postId);
    if (action === "retry") await retryPost(a, postId);
    revalidatePath(`/w/${slug}`, "layout");
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

// ------------------------------------------------------------------ accounts

export async function connectCredentials(slug: string, _prev: unknown, form: FormData): Promise<ActionResult> {
  try {
    const a = await actor(slug, "admin");
    const platform = z.enum(["bluesky", "mastodon"]).parse(form.get("platform"));
    let row: Record<string, unknown>;
    if (platform === "bluesky") {
      const handle = String(form.get("handle") ?? "").trim().replace(/^@/, "");
      const password = String(form.get("password") ?? "").trim();
      const service = String(form.get("service") ?? "").trim() || DEFAULT_BSKY_SERVICE;
      const session = await createBskySession(service, handle, password, fetch);
      row = {
        platform, external_id: session.did, display_name: session.handle, handle: session.handle,
        access_token_enc: encryptSecret(password), meta: { service },
      };
    } else {
      const instance = normaliseInstance(String(form.get("instance") ?? ""));
      const token = String(form.get("token") ?? "").trim();
      const me = await verifyMastodon(instance, token, fetch);
      row = {
        platform, external_id: me.id, display_name: me.display_name || me.username, handle: `${me.username}@${new URL(instance).host}`,
        avatar_url: me.avatar, access_token_enc: encryptSecret(token), meta: { instance },
      };
    }
    const { error } = await adminClient()
      .from("social_accounts")
      .upsert({ ...row, workspace_id: a.workspace.id, status: "active", last_error: null }, { onConflict: "workspace_id,platform,external_id" });
    if (error) throw error;
    revalidatePath(`/w/${slug}/accounts`);
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

export async function disconnectAccount(slug: string, accountId: string): Promise<ActionResult> {
  try {
    const a = await actor(slug, "admin");
    await adminClient().from("social_accounts").delete().eq("id", accountId).eq("workspace_id", a.workspace.id);
    revalidatePath(`/w/${slug}/accounts`);
    return { ok: true };
  } catch (err) {
    return failure(err);
  }
}

// ------------------------------------------------------------------ API keys

export async function createApiKey(_prev: unknown, form: FormData): Promise<ActionResult<{ key: string }>> {
  try {
    const user = await requireUser();
    const name = String(form.get("name") ?? "").trim() || "Claude";
    const workspaceId = String(form.get("workspace_id") ?? "") || null;
    if (workspaceId) {
      const access = await workspaceAccess(user.id, { id: workspaceId });
      if (!roleAtLeast(access.role, "viewer")) throw new AccessError("Not a member");
    }
    const { key, prefix, hash } = generateApiKey();
    const { error } = await adminClient()
      .from("api_keys")
      .insert({ user_id: user.id, workspace_id: workspaceId, name, key_prefix: prefix, key_hash: hash });
    if (error) throw error;
    revalidatePath("/settings/api-keys");
    return { ok: true, key };
  } catch (err) {
    return failure(err);
  }
}

export async function revokeApiKey(id: string): Promise<ActionResult> {
  const user = await requireUser();
  await adminClient().from("api_keys").update({ revoked_at: new Date().toISOString() }).eq("id", id).eq("user_id", user.id);
  revalidatePath("/settings/api-keys");
  return { ok: true };
}
