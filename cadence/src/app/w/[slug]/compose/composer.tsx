"use client";

import { useRouter } from "next/navigation";
import { useState, useTransition } from "react";
import { savePost } from "@/app/actions";
import { PlatformIcon, StatusBadge } from "@/components/ui";
import { countChars, PLATFORM_SPECS, validateForPlatform } from "@/lib/platforms/limits";
import { browserClient } from "@/lib/supabase/browser";
import { zonedLocalToIso } from "@/lib/time";
import type { MediaItem, Platform, PostSource, PostStatus, SocialAccount } from "@/lib/types";

export interface ComposerInitial {
  id: string;
  status: PostStatus;
  content: string;
  media: MediaItem[];
  accountIds: string[];
  overrides: Record<string, string>;
  scheduledLocal: string;
  reviewNote: string | null;
  source: PostSource;
}

interface Draft {
  platform: Platform;
  content: string;
  notes: string;
}

export function Composer(props: {
  slug: string;
  workspaceId: string;
  timezone: string;
  accounts: SocialAccount[];
  initial: ComposerInitial | null;
  defaultLocal: string;
  needsApproval: boolean;
  aiEnabled: boolean;
}) {
  const { slug, workspaceId, timezone, accounts, initial, needsApproval, aiEnabled } = props;
  const router = useRouter();
  const [content, setContent] = useState(initial?.content ?? "");
  const [selected, setSelected] = useState<string[]>(initial?.accountIds ?? (accounts.length === 1 ? [accounts[0].id] : []));
  const [overrides, setOverrides] = useState<Record<string, string>>(initial?.overrides ?? {});
  const [activeTab, setActiveTab] = useState<string>("main");
  const [media, setMedia] = useState<MediaItem[]>(initial?.media ?? []);
  const [when, setWhen] = useState<"later" | "now">(initial?.scheduledLocal || props.defaultLocal ? "later" : "now");
  const [local, setLocal] = useState(initial?.scheduledLocal || props.defaultLocal);
  const [error, setError] = useState<{ message: string; issues?: string[] } | null>(null);
  const [saving, startSave] = useTransition();

  const selectedAccounts = accounts.filter((a) => selected.includes(a.id));
  const platforms = [...new Set(selectedAccounts.map((a) => a.platform))];
  const textFor = (a: SocialAccount) => overrides[a.id] ?? content;

  const issues = selectedAccounts.flatMap((a) =>
    validateForPlatform(a.platform, overrides[a.id] ?? content, media).map((i) => i.message),
  );
  const uniqueIssues = [...new Set(issues)];

  function toggle(id: string) {
    setSelected((s) => (s.includes(id) ? s.filter((x) => x !== id) : [...s, id]));
  }

  function save(intent: "draft" | "schedule" | "publish_now") {
    setError(null);
    let scheduledAt: string | null = null;
    if (intent !== "publish_now" && when === "later") {
      if (!local) return setError({ message: "Pick a date and time, or choose 'As soon as possible'." });
      scheduledAt = zonedLocalToIso(local, timezone);
      if (intent === "schedule" && new Date(scheduledAt) < new Date()) {
        return setError({ message: "That time is in the past." });
      }
    }
    const cleanOverrides = Object.fromEntries(Object.entries(overrides).filter(([id, v]) => selected.includes(id) && v !== content));
    startSave(async () => {
      const res = await savePost(slug, initial?.id ?? null, {
        content, accountIds: selected, overrides: cleanOverrides, media, scheduledAt, intent,
      });
      if (!res.ok) return setError({ message: res.error, issues: res.issues });
      router.push(res.status === "pending_approval" ? `/w/${slug}/approvals` : `/w/${slug}`);
      router.refresh();
    });
  }

  if (accounts.length === 0) {
    return (
      <div className="card mx-auto max-w-xl p-6 text-center">
        <p className="font-medium">Connect a social account first</p>
        <a className="btn btn-primary mt-3" href={`/w/${slug}/accounts`}>Connect accounts</a>
      </div>
    );
  }

  const activeAccount = selectedAccounts.find((a) => a.id === activeTab);
  const editorValue = activeAccount ? textFor(activeAccount) : content;

  return (
    <div className="mx-auto grid max-w-6xl gap-6 lg:grid-cols-[minmax(0,1fr)_340px]">
      <div className="space-y-5">
        <div className="flex flex-wrap items-center gap-3">
          <h1 className="h1">{initial ? "Edit post" : "New post"}</h1>
          {initial && <StatusBadge status={initial.status} />}
        </div>
        {initial?.reviewNote && <p className="rounded-lg bg-bad-soft px-3 py-2 text-sm text-bad">Changes requested: {initial.reviewNote}</p>}

        <section>
          <p className="label">Post to</p>
          <div className="flex flex-wrap gap-2">
            {accounts.map((a) => {
              const on = selected.includes(a.id);
              return (
                <button key={a.id} type="button" onClick={() => toggle(a.id)} aria-pressed={on}
                  className={`flex items-center gap-2 rounded-full border px-3 py-1.5 text-sm transition ${
                    on ? "border-accent bg-accent-soft" : "border-line bg-surface text-muted hover:text-fg"}`}>
                  <PlatformIcon platform={a.platform} size={18} />
                  {a.display_name}
                  {a.status === "needs_reauth" && <span className="text-xs text-bad">reconnect</span>}
                </button>
              );
            })}
          </div>
        </section>

        <section className="card overflow-hidden">
          {selectedAccounts.length > 1 && (
            <div className="flex gap-1 overflow-x-auto border-b border-line bg-surface-2 px-2 pt-2">
              <Tab active={activeTab === "main"} onClick={() => setActiveTab("main")}>All accounts</Tab>
              {selectedAccounts.map((a) => (
                <Tab key={a.id} active={activeTab === a.id} onClick={() => setActiveTab(a.id)}>
                  <PlatformIcon platform={a.platform} size={14} /> {a.display_name}
                  {overrides[a.id] !== undefined && <span className="text-accent">•</span>}
                </Tab>
              ))}
            </div>
          )}
          <textarea
            className="block min-h-48 w-full resize-y bg-surface p-4 text-[15px] leading-relaxed outline-none"
            placeholder={activeAccount ? `Custom text for ${activeAccount.display_name}` : "What do you want to share?"}
            value={editorValue}
            onChange={(e) =>
              activeAccount ? setOverrides((o) => ({ ...o, [activeAccount.id]: e.target.value })) : setContent(e.target.value)
            }
          />
          <div className="flex flex-wrap items-center gap-3 border-t border-line px-4 py-2 text-xs">
            {(activeAccount ? [activeAccount.platform] : platforms).map((p) => {
              const text = activeAccount ? editorValue : content;
              const n = countChars(p, text);
              const max = PLATFORM_SPECS[p].maxChars;
              const usesMain = !activeAccount && selectedAccounts.filter((a) => a.platform === p).every((a) => overrides[a.id] !== undefined);
              return (
                <span key={p} className={`flex items-center gap-1 ${n > max ? "font-medium text-bad" : "text-muted"} ${usesMain ? "line-through opacity-60" : ""}`}>
                  <PlatformIcon platform={p} size={14} /> {n}/{max}
                </span>
              );
            })}
            {activeAccount && overrides[activeAccount.id] !== undefined && (
              <button className="ml-auto text-muted underline" onClick={() => setOverrides((o) => Object.fromEntries(Object.entries(o).filter(([id]) => id !== activeAccount.id)))}>
                Use the main text instead
              </button>
            )}
          </div>
        </section>

        <MediaPicker workspaceId={workspaceId} media={media} setMedia={setMedia} />

        <section className="card space-y-3 p-4">
          <p className="label mb-0">When</p>
          <div className="flex flex-wrap gap-4 text-sm">
            <label className="flex items-center gap-2"><input type="radio" checked={when === "now"} onChange={() => setWhen("now")} /> As soon as possible</label>
            <label className="flex items-center gap-2"><input type="radio" checked={when === "later"} onChange={() => setWhen("later")} /> Schedule for</label>
          </div>
          {when === "later" && (
            <div className="flex flex-wrap items-center gap-2">
              <input type="datetime-local" className="input max-w-60" value={local} onChange={(e) => setLocal(e.target.value)} />
              <span className="hint">{timezone.replaceAll("_", " ")}</span>
            </div>
          )}
        </section>

        {uniqueIssues.length > 0 && (
          <ul className="space-y-1 rounded-lg bg-warn-soft px-4 py-3 text-sm text-warn">
            {uniqueIssues.map((i) => <li key={i}>{i}</li>)}
          </ul>
        )}
        {error && (
          <div className="rounded-lg bg-bad-soft px-4 py-3 text-sm text-bad">
            <p>{error.message}</p>
            {error.issues && <ul className="mt-1 list-disc pl-5">{error.issues.map((i) => <li key={i}>{i}</li>)}</ul>}
          </div>
        )}

        <div className="flex flex-wrap gap-2">
          <button className="btn btn-primary" disabled={saving || !selected.length || uniqueIssues.length > 0} onClick={() => save("schedule")}>
            {needsApproval ? "Submit for approval" : when === "now" ? "Publish now" : "Schedule"}
          </button>
          <button className="btn" disabled={saving || !selected.length} onClick={() => save("draft")}>Save draft</button>
          {saving && <span className="self-center text-sm text-muted">Saving…</span>}
        </div>
      </div>

      <aside className="space-y-4">
        {aiEnabled ? (
          <AiPanel
            workspaceId={workspaceId}
            platforms={platforms}
            onUse={(d) => {
              const targets = selectedAccounts.filter((a) => a.platform === d.platform);
              if (platforms.length === 1 || !content) {
                setContent(d.content);
                // Keep other platforms' overrides; this text becomes the main version.
              } else {
                setOverrides((o) => ({ ...o, ...Object.fromEntries(targets.map((a) => [a.id, d.content])) }));
              }
            }}
            onUseAll={(drafts) => {
              const [first, ...rest] = drafts;
              if (!first) return;
              setContent(first.content);
              const next: Record<string, string> = {};
              for (const d of rest) for (const a of selectedAccounts.filter((x) => x.platform === d.platform)) next[a.id] = d.content;
              setOverrides(next);
            }}
          />
        ) : (
          <div className="card p-4 text-sm text-muted">
            <p className="h2 mb-1 text-fg">Write with Claude</p>
            Set <code>ANTHROPIC_API_KEY</code> to draft posts here, or connect Claude through MCP (see <a className="underline" href="/settings/api-keys">Claude &amp; API</a>).
          </div>
        )}
        <Previews accounts={selectedAccounts} textFor={textFor} media={media} />
      </aside>
    </div>
  );
}

function Tab({ active, onClick, children }: { active: boolean; onClick: () => void; children: React.ReactNode }) {
  return (
    <button type="button" onClick={onClick}
      className={`flex shrink-0 items-center gap-1.5 rounded-t-lg px-3 py-1.5 text-xs ${active ? "bg-surface font-medium" : "text-muted hover:text-fg"}`}>
      {children}
    </button>
  );
}

function MediaPicker({ workspaceId, media, setMedia }: { workspaceId: string; media: MediaItem[]; setMedia: (m: MediaItem[]) => void }) {
  const [uploading, setUploading] = useState(false);
  const [url, setUrl] = useState("");
  const [err, setErr] = useState<string | null>(null);

  async function upload(files: FileList | null) {
    if (!files?.length) return;
    setUploading(true);
    setErr(null);
    const supabase = browserClient();
    const added: MediaItem[] = [];
    for (const file of Array.from(files)) {
      const path = `${workspaceId}/${crypto.randomUUID()}-${file.name.replace(/[^\w.-]/g, "_")}`;
      const { error } = await supabase.storage.from("media").upload(path, file, { contentType: file.type });
      if (error) {
        setErr(error.message);
        continue;
      }
      const { data } = supabase.storage.from("media").getPublicUrl(path);
      added.push({ url: data.publicUrl, type: file.type.startsWith("video") ? "video" : "image", alt: "" });
    }
    setMedia([...media, ...added]);
    setUploading(false);
  }

  return (
    <section className="card space-y-3 p-4">
      <div className="flex flex-wrap items-center gap-2">
        <p className="label mb-0 mr-auto">Images &amp; video</p>
        <label className="btn btn-sm cursor-pointer">
          {uploading ? "Uploading…" : "Upload"}
          <input type="file" accept="image/*,video/mp4,video/quicktime" multiple hidden onChange={(e) => upload(e.target.files)} />
        </label>
      </div>
      <div className="flex gap-2">
        <input className="input" placeholder="…or paste a public image URL" value={url} onChange={(e) => setUrl(e.target.value)} />
        <button className="btn btn-sm" disabled={!/^https:\/\//.test(url)} onClick={() => { setMedia([...media, { url, type: /\.(mp4|mov)$/i.test(url) ? "video" : "image", alt: "" }]); setUrl(""); }}>
          Add
        </button>
      </div>
      {err && <p className="text-sm text-bad">{err}</p>}
      {media.length > 0 && (
        <ul className="space-y-2">
          {media.map((m, i) => (
            <li key={m.url} className="flex items-center gap-3">
              {m.type === "image" ? (
                // eslint-disable-next-line @next/next/no-img-element
                <img src={m.url} alt="" className="h-14 w-14 shrink-0 rounded-md border border-line object-cover" />
              ) : (
                <span className="flex h-14 w-14 shrink-0 items-center justify-center rounded-md border border-line text-xs">Video</span>
              )}
              <input className="input" placeholder="Alt text (describe the image)" value={m.alt ?? ""}
                onChange={(e) => setMedia(media.map((x, j) => (j === i ? { ...x, alt: e.target.value } : x)))} />
              <button className="btn btn-sm btn-danger" onClick={() => setMedia(media.filter((_, j) => j !== i))} aria-label="Remove">✕</button>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}

function AiPanel({ workspaceId, platforms, onUse, onUseAll }: {
  workspaceId: string;
  platforms: Platform[];
  onUse: (d: Draft) => void;
  onUseAll: (d: Draft[]) => void;
}) {
  const [brief, setBrief] = useState("");
  const [drafts, setDrafts] = useState<Draft[]>([]);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  async function generate() {
    setBusy(true);
    setErr(null);
    try {
      const res = await fetch("/api/ai/draft", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ workspaceId, brief, platforms }),
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error ?? "Drafting failed");
      setDrafts(json.drafts);
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="card space-y-3 p-4">
      <p className="h2">Write with Claude</p>
      <textarea className="input min-h-24" placeholder="What's the post about? e.g. 'Announce our new Saturday opening hours, 9–2, starting 6 Dec. Friendly, local.'"
        value={brief} onChange={(e) => setBrief(e.target.value)} />
      <button className="btn btn-primary w-full" disabled={busy || brief.trim().length < 3 || platforms.length === 0} onClick={generate}>
        {busy ? "Drafting…" : platforms.length ? `Draft for ${platforms.map((p) => PLATFORM_SPECS[p].label).join(", ")}` : "Choose accounts first"}
      </button>
      <p className="hint">Uses the workspace brand voice. Always check facts before scheduling.</p>
      {err && <p className="text-sm text-bad">{err}</p>}
      {drafts.length > 1 && <button className="btn btn-sm w-full" onClick={() => onUseAll(drafts)}>Use all drafts</button>}
      {drafts.map((d, i) => (
        <div key={i} className="rounded-lg border border-line p-3">
          <div className="mb-1 flex items-center gap-2 text-xs text-muted">
            <PlatformIcon platform={d.platform} size={16} /> {PLATFORM_SPECS[d.platform].label}
            <span className="ml-auto">{countChars(d.platform, d.content)}/{PLATFORM_SPECS[d.platform].maxChars}</span>
          </div>
          <p className="whitespace-pre-wrap text-sm">{d.content}</p>
          {d.notes && <p className="hint mt-1">{d.notes}</p>}
          <button className="btn btn-sm mt-2" onClick={() => onUse(d)}>Use this</button>
        </div>
      ))}
    </div>
  );
}

function Previews({ accounts, textFor, media }: { accounts: SocialAccount[]; textFor: (a: SocialAccount) => string; media: MediaItem[] }) {
  if (!accounts.length) return null;
  return (
    <div className="space-y-3">
      <p className="h2">Preview</p>
      {accounts.map((a) => (
        <div key={a.id} className="card p-3">
          <div className="mb-2 flex items-center gap-2">
            <PlatformIcon platform={a.platform} />
            <div className="min-w-0 text-sm leading-tight">
              <p className="truncate font-medium">{a.display_name}</p>
              {a.handle && <p className="truncate text-xs text-muted">@{a.handle}</p>}
            </div>
          </div>
          <p className="line-clamp-6 whitespace-pre-wrap text-sm">{textFor(a) || <span className="text-muted">Nothing yet</span>}</p>
          {media[0]?.type === "image" && (
            // eslint-disable-next-line @next/next/no-img-element
            <img src={media[0].url} alt={media[0].alt ?? ""} className="mt-2 max-h-48 w-full rounded-lg object-cover" />
          )}
        </div>
      ))}
    </div>
  );
}
