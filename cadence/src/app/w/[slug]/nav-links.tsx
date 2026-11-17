"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

export function NavLinks({ slug, pendingCount, canAdmin }: { slug: string; pendingCount: number; canAdmin: boolean }) {
  const path = usePathname();
  const base = `/w/${slug}`;
  const items = [
    { href: `${base}/compose`, label: "New post", primary: true },
    { href: base, label: "Queue" },
    { href: `${base}/calendar`, label: "Calendar" },
    { href: `${base}/approvals`, label: "Approvals", badge: pendingCount },
    { href: `${base}/accounts`, label: "Accounts" },
    ...(canAdmin ? [{ href: `${base}/settings`, label: "Settings" }] : []),
    { href: "/settings/api-keys", label: "Claude & API", mobileOnly: true },
  ];
  return (
    <nav className="flex gap-1 overflow-x-auto md:flex-col">
      {items.map((item) => {
        const active = item.href === base ? path === base : path.startsWith(item.href);
        if (item.primary) {
          return <Link key={item.href} href={item.href} className="btn btn-primary mb-2 shrink-0 md:w-full">{item.label}</Link>;
        }
        return (
          <Link
            key={item.href}
            href={item.href}
            className={`flex shrink-0 items-center justify-between rounded-lg px-3 py-2 text-sm ${item.mobileOnly ? "md:hidden" : ""} ${
              active ? "bg-surface-2 font-medium" : "text-muted hover:bg-surface-2 hover:text-fg"
            }`}
          >
            {item.label}
            {!!item.badge && <span className="ml-2 rounded-full bg-warn-soft px-1.5 text-xs font-medium text-warn">{item.badge}</span>}
          </Link>
        );
      })}
    </nav>
  );
}
