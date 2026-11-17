import type { Platform, PostStatus } from "@/lib/types";
import { PLATFORM_SPECS } from "@/lib/platforms/limits";

const STATUS_STYLE: Record<PostStatus, { label: string; cls: string }> = {
  draft: { label: "Draft", cls: "bg-surface-2 text-muted" },
  pending_approval: { label: "Needs approval", cls: "bg-warn-soft text-warn" },
  scheduled: { label: "Scheduled", cls: "bg-accent-soft text-accent" },
  publishing: { label: "Publishing", cls: "bg-accent-soft text-accent" },
  published: { label: "Published", cls: "bg-ok-soft text-ok" },
  partially_failed: { label: "Partly failed", cls: "bg-bad-soft text-bad" },
  failed: { label: "Failed", cls: "bg-bad-soft text-bad" },
  rejected: { label: "Rejected", cls: "bg-bad-soft text-bad" },
};

export function StatusBadge({ status }: { status: PostStatus }) {
  const s = STATUS_STYLE[status];
  return <span className={`inline-flex rounded-full px-2 py-0.5 text-xs font-medium ${s.cls}`}>{s.label}</span>;
}

const PLATFORM_COLOR: Record<Platform, string> = {
  linkedin: "#0a66c2",
  x: "#111111",
  facebook: "#1877f2",
  instagram: "#d62976",
  threads: "#333333",
  bluesky: "#1185fe",
  mastodon: "#6364ff",
};

const PLATFORM_LETTER: Record<Platform, string> = {
  linkedin: "in", x: "X", facebook: "f", instagram: "IG", threads: "@", bluesky: "bs", mastodon: "M",
};

export function PlatformIcon({ platform, size = 20 }: { platform: Platform; size?: number }) {
  return (
    <span
      title={PLATFORM_SPECS[platform].label}
      className="inline-flex shrink-0 items-center justify-center rounded-md font-semibold text-white"
      style={{ width: size, height: size, background: PLATFORM_COLOR[platform], fontSize: size * 0.42 }}
    >
      {PLATFORM_LETTER[platform]}
    </span>
  );
}

export function EmptyState({ title, children }: { title: string; children?: React.ReactNode }) {
  return (
    <div className="card flex flex-col items-center gap-2 px-6 py-10 text-center">
      <p className="font-medium">{title}</p>
      {children && <div className="max-w-md text-sm text-muted">{children}</div>}
    </div>
  );
}

export function formatWhen(iso: string | null, timezone: string) {
  if (!iso) return "Not scheduled";
  return new Intl.DateTimeFormat("en-GB", {
    timeZone: timezone, weekday: "short", day: "numeric", month: "short", hour: "2-digit", minute: "2-digit",
  }).format(new Date(iso));
}
