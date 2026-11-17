import { describe, expect, it } from "vitest";
import { encryptSecret, decryptSecret, generateApiKey, hashApiKey } from "@/lib/crypto";
import { buildFacets } from "@/lib/platforms/bluesky";
import { countChars, validateForPlatform } from "@/lib/platforms/limits";
import { toLinkedInCommentary } from "@/lib/platforms/linkedin";
import { isoToZonedLocal, zonedLocalToIso } from "@/lib/time";
import { decideStatus, retryDelayMs, summarise } from "@/lib/workflow";

process.env.TOKEN_ENCRYPTION_KEY = Buffer.alloc(32, 7).toString("base64");

describe("character counting", () => {
  it("counts X links as 23 characters", () => {
    const text = "Read this https://example.com/a/very/long/path/that/goes/on/and/on/forever";
    expect(countChars("x", text)).toBe("Read this ".length + 23);
  });
  it("counts Bluesky graphemes, so an emoji family is one character", () => {
    expect(countChars("bluesky", "hi 👨‍👩‍👧")).toBe(4);
  });
  it("flags posts that are too long or missing required media", () => {
    expect(validateForPlatform("x", "a".repeat(281), [])).toHaveLength(1);
    expect(validateForPlatform("x", "a".repeat(280), [])).toHaveLength(0);
    expect(validateForPlatform("instagram", "caption", [])[0].message).toMatch(/needs at least one image/);
    expect(validateForPlatform("linkedin", "   ", [])[0].message).toMatch(/empty/);
  });
});

describe("approval workflow", () => {
  const now = new Date("2026-11-17T10:00:00Z");
  const base = { requireApproval: true, scheduledAt: "2026-11-20T09:00:00Z", now } as const;

  it("sends Claude's posts for approval even when written by an owner's key", () => {
    expect(decideStatus({ ...base, intent: "schedule", role: "owner", source: "mcp" }).status).toBe("pending_approval");
  });
  it("lets owners and admins schedule directly in the web app", () => {
    const r = decideStatus({ ...base, intent: "schedule", role: "admin", source: "web" });
    expect(r).toEqual({ status: "scheduled", scheduledAt: "2026-11-20T09:00:00.000Z" });
  });
  it("sends editors' posts for approval", () => {
    expect(decideStatus({ ...base, intent: "schedule", role: "editor", source: "web" }).status).toBe("pending_approval");
  });
  it("skips approval when the workspace turns it off", () => {
    expect(decideStatus({ ...base, requireApproval: false, intent: "schedule", role: "editor", source: "mcp" }).status).toBe("scheduled");
  });
  it("never schedules drafts and moves past times to now", () => {
    expect(decideStatus({ ...base, intent: "draft", role: "owner", source: "web" }).status).toBe("draft");
    const past = decideStatus({ ...base, scheduledAt: "2026-01-01T00:00:00Z", intent: "schedule", role: "owner", source: "web" });
    expect(past.scheduledAt).toBe(now.toISOString());
  });
  it("summarises target outcomes", () => {
    expect(summarise([{ status: "published" }, { status: "published" }])).toBe("published");
    expect(summarise([{ status: "published" }, { status: "failed" }])).toBe("partially_failed");
    expect(summarise([{ status: "failed" }])).toBe("failed");
    expect(summarise([{ status: "published" }, { status: "pending" }])).toBe("scheduled");
    expect(retryDelayMs(1)).toBe(120_000);
  });
});

describe("timezones", () => {
  it("converts wall-clock time in London across the DST change", () => {
    expect(zonedLocalToIso("2026-07-01T09:00", "Europe/London")).toBe("2026-07-01T08:00:00.000Z");
    expect(zonedLocalToIso("2026-12-01T09:00", "Europe/London")).toBe("2026-12-01T09:00:00.000Z");
    expect(zonedLocalToIso("2026-11-20T09:30", "America/New_York")).toBe("2026-11-20T14:30:00.000Z");
  });
  it("round-trips", () => {
    expect(isoToZonedLocal(zonedLocalToIso("2026-03-29T12:15", "Europe/London"), "Europe/London")).toBe("2026-03-29T12:15");
  });
});

describe("formatting", () => {
  it("escapes LinkedIn reserved characters and keeps hashtags", () => {
    expect(toLinkedInCommentary("Hello (world) #launch @team")).toBe("Hello \\(world\\) {hashtag|\\#|launch} \\@team");
  });
  it("builds Bluesky facets with UTF-8 byte offsets", () => {
    const text = "☕ new menu https://cafe.example #coffee";
    const facets = buildFacets(text);
    const bytes = new TextEncoder().encode(text);
    const slice = (f: (typeof facets)[number]) => new TextDecoder().decode(bytes.slice(f.index.byteStart, f.index.byteEnd));
    expect(facets.map(slice)).toEqual(["https://cafe.example", "#coffee"]);
  });
});

describe("secrets", () => {
  it("encrypts and decrypts tokens, and detects tampering", () => {
    const enc = encryptSecret("token-123");
    expect(enc).not.toContain("token-123");
    expect(decryptSecret(enc)).toBe("token-123");
    const parts = enc.split(".");
    parts[3] = Buffer.from("tampered").toString("base64url");
    expect(() => decryptSecret(parts.join("."))).toThrow();
  });
  it("generates API keys that are only stored as hashes", () => {
    const { key, hash, prefix } = generateApiKey();
    expect(key.startsWith("cad_")).toBe(true);
    expect(hash).toBe(hashApiKey(key));
    expect(key.startsWith(prefix)).toBe(true);
  });
});
