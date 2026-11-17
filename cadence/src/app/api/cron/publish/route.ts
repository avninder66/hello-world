import { timingSafeEqual } from "node:crypto";
import { runPublisher } from "@/lib/publisher";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";
export const maxDuration = 300;

function authorised(request: Request): boolean {
  const secret = process.env.CRON_SECRET;
  if (!secret) return false;
  const given = (request.headers.get("authorization") ?? "").replace(/^Bearer\s+/i, "");
  const a = Buffer.from(given);
  const b = Buffer.from(secret);
  return a.length === b.length && timingSafeEqual(a, b);
}

/** Publishes due posts. Call every minute (Vercel Cron or Supabase pg_cron, see README). */
async function handle(request: Request) {
  if (!authorised(request)) return Response.json({ error: "unauthorised" }, { status: 401 });
  const summary = await runPublisher();
  return Response.json(summary);
}

export { handle as GET, handle as POST };
