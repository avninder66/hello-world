import { describe, expect, it } from "vitest";
import { ADAPTERS, PlatformError, type PublishAccount } from "@/lib/platforms";

type Call = { url: string; init?: RequestInit };

/** Fake fetch that answers by URL substring, recording every call. */
function fakeFetch(routes: [string, () => Response][]) {
  const calls: Call[] = [];
  const impl = (async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    calls.push({ url, init });
    const route = routes.find(([match]) => url.includes(match));
    if (!route) throw new Error(`Unexpected request: ${url}`);
    return route[1]();
  }) as typeof fetch;
  return { impl, calls };
}

const json = (body: unknown, init: ResponseInit = {}) =>
  new Response(JSON.stringify(body), { status: 200, headers: { "content-type": "application/json", ...(init.headers ?? {}) }, ...init });

const account = (over: Partial<PublishAccount>): PublishAccount => ({
  id: "acc", platform: "x", externalId: "ext", handle: "acme", accessToken: "tok", refreshToken: null,
  tokenExpiresAt: null, meta: {}, ...over,
});
const img = { url: "https://cdn.example/a.jpg", type: "image" as const, alt: "A cake" };
const image = () => new Response(new Blob([new Uint8Array([1, 2, 3])]), { headers: { "content-type": "image/jpeg" } });

describe("LinkedIn", () => {
  it("posts as the member with versioned headers", async () => {
    const f = fakeFetch([["/rest/posts", () => new Response(null, { status: 201, headers: { "x-restli-id": "urn:li:share:9" } })]]);
    const r = await ADAPTERS.linkedin.publish(
      { account: account({ platform: "linkedin", meta: { urn: "urn:li:person:abc" } }), text: "Hi #news", media: [], idempotencyKey: "t1" },
      f.impl,
    );
    expect(r.externalId).toBe("urn:li:share:9");
    const body = JSON.parse(String(f.calls[0].init?.body));
    expect(body.author).toBe("urn:li:person:abc");
    expect(body.commentary).toBe("Hi {hashtag|\\#|news}");
    expect((f.calls[0].init?.headers as Record<string, string>)["LinkedIn-Version"]).toMatch(/^\d{6}$/);
  });
  it("uploads an image before posting", async () => {
    const f = fakeFetch([
      ["initializeUpload", () => json({ value: { uploadUrl: "https://upload.linkedin.example/1", image: "urn:li:image:1" } })],
      ["cdn.example", image],
      ["upload.linkedin.example", () => new Response(null, { status: 201 })],
      ["/rest/posts", () => new Response(null, { status: 201, headers: { "x-restli-id": "urn:li:share:10" } })],
    ]);
    await ADAPTERS.linkedin.publish({ account: account({ platform: "linkedin" }), text: "Cake", media: [img], idempotencyKey: "t" }, f.impl);
    const post = JSON.parse(String(f.calls.at(-1)!.init?.body));
    expect(post.content.media).toEqual({ id: "urn:li:image:1", altText: "A cake" });
  });
});

describe("X", () => {
  it("posts a tweet and builds its URL", async () => {
    const f = fakeFetch([["/2/tweets", () => json({ data: { id: "123" } })]]);
    const r = await ADAPTERS.x.publish({ account: account({}), text: "hello", media: [], idempotencyKey: "t" }, f.impl);
    expect(r).toEqual({ externalId: "123", url: "https://x.com/acme/status/123" });
  });
  it("classifies rate limits as retryable and 401 as needing reauth", async () => {
    const limited = fakeFetch([["/2/tweets", () => new Response("slow down", { status: 429 })]]);
    const err = await ADAPTERS.x.publish({ account: account({}), text: "x", media: [], idempotencyKey: "t" }, limited.impl).catch((e) => e);
    expect(err).toBeInstanceOf(PlatformError);
    expect(err.retryable).toBe(true);
    const unauth = fakeFetch([["/2/tweets", () => new Response("no", { status: 401 })]]);
    const err2 = await ADAPTERS.x.publish({ account: account({}), text: "x", media: [], idempotencyKey: "t" }, unauth.impl).catch((e) => e);
    expect(err2.needsReauth).toBe(true);
    expect(err2.retryable).toBe(false);
  });
  it("refreshes tokens with rotation", async () => {
    process.env.X_CLIENT_ID = "id";
    process.env.X_CLIENT_SECRET = "secret";
    const f = fakeFetch([["/2/oauth2/token", () => json({ access_token: "new", refresh_token: "r2", expires_in: 7200 })]]);
    const t = await ADAPTERS.x.refresh!(account({ refreshToken: "r1" }), f.impl);
    expect(t.accessToken).toBe("new");
    expect(t.refreshToken).toBe("r2");
    expect(String(f.calls[0].init?.body)).toContain("grant_type=refresh_token");
  });
});

describe("Facebook & Instagram", () => {
  it("attaches multiple unpublished photos to one Page post", async () => {
    let n = 0;
    const f = fakeFetch([
      ["/photos", () => json({ id: `photo${++n}` })],
      ["/feed", () => json({ id: "page_post" })],
    ]);
    const r = await ADAPTERS.facebook.publish(
      { account: account({ platform: "facebook", externalId: "page1" }), text: "Two", media: [img, { ...img, url: "https://cdn.example/b.jpg" }], idempotencyKey: "t" },
      f.impl,
    );
    expect(r.externalId).toBe("page_post");
    const feed = new URLSearchParams(String(f.calls.at(-1)!.init?.body));
    expect(feed.get("attached_media[1]")).toBe('{"media_fbid":"photo2"}');
  });
  it("creates and publishes an Instagram container", async () => {
    const f = fakeFetch([
      ["/ig1/media_publish", () => json({ id: "media9" })],
      ["/ig1/media", () => json({ id: "container1" })],
      ["/media9", () => json({ permalink: "https://instagram.com/p/x" })],
    ]);
    const r = await ADAPTERS.instagram.publish(
      { account: account({ platform: "instagram", externalId: "ig1" }), text: "Caption", media: [img], idempotencyKey: "t" },
      f.impl,
    );
    expect(r).toEqual({ externalId: "media9", url: "https://instagram.com/p/x" });
    const create = new URLSearchParams(String(f.calls[0].init?.body));
    expect(create.get("image_url")).toBe(img.url);
    expect(create.get("caption")).toBe("Caption");
  });
  it("refuses Instagram posts without media", async () => {
    await expect(
      ADAPTERS.instagram.publish({ account: account({ platform: "instagram" }), text: "x", media: [], idempotencyKey: "t" }, fetch),
    ).rejects.toThrow(/image or video/);
  });
});

describe("Threads", () => {
  it("publishes a text thread in two steps", async () => {
    const f = fakeFetch([
      ["/u1/threads_publish", () => json({ id: "th9" })],
      ["/u1/threads", () => json({ id: "c1" })],
      ["/th9", () => json({ permalink: "https://threads.net/@acme/post/x" })],
    ]);
    const r = await ADAPTERS.threads.publish({ account: account({ platform: "threads", externalId: "u1" }), text: "hey", media: [], idempotencyKey: "t" }, f.impl);
    expect(r.externalId).toBe("th9");
    expect(new URLSearchParams(String(f.calls[0].init?.body)).get("media_type")).toBe("TEXT");
  });
});

describe("Bluesky", () => {
  it("logs in with the app password and creates a record with facets", async () => {
    const f = fakeFetch([
      ["createSession", () => json({ accessJwt: "jwt", did: "did:plc:1", handle: "acme.bsky.social" })],
      ["createRecord", () => json({ uri: "at://did:plc:1/app.bsky.feed.post/3abc" })],
    ]);
    const r = await ADAPTERS.bluesky.publish(
      { account: account({ platform: "bluesky", handle: "acme.bsky.social", accessToken: "app-pass" }), text: "See https://acme.example", media: [], idempotencyKey: "t" },
      f.impl,
    );
    expect(r.url).toBe("https://bsky.app/profile/acme.bsky.social/post/3abc");
    const rec = JSON.parse(String(f.calls[1].init?.body)).record;
    expect(rec.facets[0].features[0].uri).toBe("https://acme.example");
    expect(JSON.parse(String(f.calls[0].init?.body))).toEqual({ identifier: "acme.bsky.social", password: "app-pass" });
  });
});

describe("Mastodon", () => {
  it("posts with an idempotency key", async () => {
    const f = fakeFetch([["/api/v1/statuses", () => json({ id: "77", url: "https://mastodon.social/@acme/77" })]]);
    const r = await ADAPTERS.mastodon.publish(
      { account: account({ platform: "mastodon", meta: { instance: "https://mastodon.social" } }), text: "toot", media: [], idempotencyKey: "target-1" },
      f.impl,
    );
    expect(r.externalId).toBe("77");
    expect((f.calls[0].init?.headers as Record<string, string>)["Idempotency-Key"]).toBe("target-1");
  });
});
