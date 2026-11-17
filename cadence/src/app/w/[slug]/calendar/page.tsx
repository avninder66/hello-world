import Link from "next/link";
import { PlatformIcon } from "@/components/ui";
import { requireWorkspacePage } from "@/lib/auth";
import { listPosts } from "@/lib/posts";
import type { PostStatus } from "@/lib/types";

const DOT: Partial<Record<PostStatus, string>> = {
  pending_approval: "border-l-warn", scheduled: "border-l-accent", publishing: "border-l-accent",
  published: "border-l-ok", failed: "border-l-bad", partially_failed: "border-l-bad",
};

/** Date parts of an instant in a timezone. */
function partsIn(date: Date, tz: string) {
  const p = Object.fromEntries(
    new Intl.DateTimeFormat("en-GB", { timeZone: tz, year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hourCycle: "h23" })
      .formatToParts(date).map((x) => [x.type, x.value]),
  );
  return { key: `${p.year}-${p.month}-${p.day}`, time: `${p.hour}:${p.minute}` };
}

export default async function CalendarPage({ params, searchParams }: PageProps<"/w/[slug]/calendar">) {
  const { slug } = await params;
  const sp = await searchParams;
  const access = await requireWorkspacePage(slug);
  const tz = access.workspace.timezone;

  const today = partsIn(new Date(), tz).key;
  const month = typeof sp.month === "string" && /^\d{4}-\d{2}$/.test(sp.month) ? sp.month : today.slice(0, 7);
  const [y, m] = month.split("-").map(Number);
  const first = new Date(Date.UTC(y, m - 1, 1));
  const startOffset = (first.getUTCDay() + 6) % 7; // Monday first
  const gridStart = new Date(Date.UTC(y, m - 1, 1 - startOffset));
  const days = Array.from({ length: 42 }, (_, i) => new Date(gridStart.getTime() + i * 86_400_000));

  // Pad the query by a day either side to cover timezone offsets.
  const posts = await listPosts(access, {
    from: new Date(gridStart.getTime() - 86_400_000).toISOString(),
    to: new Date(gridStart.getTime() + 43 * 86_400_000).toISOString(),
    limit: 500,
  });
  const byDay = new Map<string, typeof posts>();
  for (const p of posts) {
    if (!p.scheduled_at || p.status === "draft" || p.status === "rejected") continue;
    const k = partsIn(new Date(p.scheduled_at), tz).key;
    byDay.set(k, [...(byDay.get(k) ?? []), p]);
  }

  const prev = new Date(Date.UTC(y, m - 2, 1)).toISOString().slice(0, 7);
  const next = new Date(Date.UTC(y, m, 1)).toISOString().slice(0, 7);
  const title = new Intl.DateTimeFormat("en-GB", { month: "long", year: "numeric", timeZone: "UTC" }).format(first);

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center gap-3">
        <h1 className="h1 mr-auto">{title}</h1>
        <Link className="btn btn-sm" href={`?month=${prev}`}>← Prev</Link>
        <Link className="btn btn-sm" href="?">Today</Link>
        <Link className="btn btn-sm" href={`?month=${next}`}>Next →</Link>
      </div>
      <div className="overflow-x-auto">
        <div className="grid min-w-[720px] grid-cols-7 gap-px overflow-hidden rounded-xl border border-line bg-line">
          {["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"].map((d) => (
            <div key={d} className="bg-surface-2 px-2 py-1.5 text-xs font-medium text-muted">{d}</div>
          ))}
          {days.map((d) => {
            const key = d.toISOString().slice(0, 10);
            const inMonth = key.startsWith(month);
            const items = (byDay.get(key) ?? []).sort((a, b) => a.scheduled_at!.localeCompare(b.scheduled_at!));
            return (
              <div key={key} className={`min-h-28 bg-surface p-1.5 ${inMonth ? "" : "opacity-45"}`}>
                <div className="mb-1 flex items-center justify-between">
                  <span className={`text-xs ${key === today ? "rounded-full bg-accent px-1.5 font-semibold text-accent-fg" : "text-muted"}`}>{d.getUTCDate()}</span>
                  {key >= today && (
                    <Link href={`/w/${slug}/compose?date=${key}`} className="text-xs text-muted hover:text-fg" aria-label={`New post on ${key}`}>+</Link>
                  )}
                </div>
                <div className="space-y-1">
                  {items.map((p) => (
                    <Link key={p.id} href={`/w/${slug}/compose?post=${p.id}`}
                      className={`block rounded-md border-l-[3px] bg-surface-2 px-1.5 py-1 text-xs hover:bg-line ${DOT[p.status] ?? "border-l-line"}`}>
                      <span className="flex items-center gap-1">
                        <span className="font-medium">{partsIn(new Date(p.scheduled_at!), tz).time}</span>
                        <span className="ml-auto flex gap-0.5">
                          {p.post_targets.slice(0, 3).map((t) => t.social_accounts && <PlatformIcon key={t.id} platform={t.social_accounts.platform} size={14} />)}
                        </span>
                      </span>
                      <span className="line-clamp-2 text-muted">{p.content}</span>
                    </Link>
                  ))}
                </div>
              </div>
            );
          })}
        </div>
      </div>
      <p className="hint">Amber = needs approval · Blue = scheduled · Green = published · Red = failed. Times in {tz.replaceAll("_", " ")}.</p>
    </div>
  );
}
