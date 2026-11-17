import { downloadMedia, PlatformError, readJson, type FetchLike, type PublisherAdapter } from "./types";

export const DEFAULT_BSKY_SERVICE = "https://bsky.social";

interface Facet {
  index: { byteStart: number; byteEnd: number };
  features: ({ $type: "app.bsky.richtext.facet#link"; uri: string } | { $type: "app.bsky.richtext.facet#tag"; tag: string })[];
}

const encoder = new TextEncoder();
const byteLen = (s: string) => encoder.encode(s).length;

/** Builds rich-text facets so links and hashtags are clickable (offsets are UTF-8 bytes). */
export function buildFacets(text: string): Facet[] {
  const facets: Facet[] = [];
  const urlRe = /https?:\/\/[^\s<>"']+[^\s<>"'.,;:!?)\]]/g;
  for (const m of text.matchAll(urlRe)) {
    const start = byteLen(text.slice(0, m.index));
    facets.push({
      index: { byteStart: start, byteEnd: start + byteLen(m[0]) },
      features: [{ $type: "app.bsky.richtext.facet#link", uri: m[0] }],
    });
  }
  const tagRe = /(^|\s)(#[\p{L}\p{N}_]*[\p{L}_][\p{L}\p{N}_]*)/gu;
  for (const m of text.matchAll(tagRe)) {
    const tagStart = m.index! + m[1].length;
    const start = byteLen(text.slice(0, tagStart));
    facets.push({
      index: { byteStart: start, byteEnd: start + byteLen(m[2]) },
      features: [{ $type: "app.bsky.richtext.facet#tag", tag: m[2].slice(1) }],
    });
  }
  return facets;
}

export async function createBskySession(service: string, identifier: string, password: string, fetchImpl: FetchLike) {
  const res = await fetchImpl(`${service}/xrpc/com.atproto.server.createSession`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ identifier, password }),
  });
  if (res.status === 401) {
    throw new PlatformError("Bluesky rejected the handle or app password", { needsReauth: true, status: 401 });
  }
  return readJson<{ accessJwt: string; did: string; handle: string }>(res, "Bluesky login");
}

export const bluesky: PublisherAdapter = {
  platform: "bluesky",
  async publish({ account, text, media }, fetchImpl) {
    const service = String(account.meta.service ?? DEFAULT_BSKY_SERVICE);
    // accessToken holds the app password; sessions are short-lived so log in per publish.
    const session = await createBskySession(service, account.handle ?? account.externalId, account.accessToken, fetchImpl);
    const auth = { Authorization: `Bearer ${session.accessJwt}` };

    const record: Record<string, unknown> = {
      $type: "app.bsky.feed.post",
      text,
      createdAt: new Date().toISOString(),
      langs: ["en"],
    };
    const facets = buildFacets(text);
    if (facets.length) record.facets = facets;

    const images = media.filter((m) => m.type === "image").slice(0, 4);
    if (images.length) {
      const embedded = [];
      for (const img of images) {
        const { bytes, contentType } = await downloadMedia(img.url, fetchImpl);
        if (bytes.size > 1_000_000) throw new PlatformError("Bluesky images must be under 1MB");
        const up = await readJson<{ blob: unknown }>(
          await fetchImpl(`${service}/xrpc/com.atproto.repo.uploadBlob`, {
            method: "POST",
            headers: { ...auth, "Content-Type": contentType },
            body: bytes,
          }),
          "Bluesky image upload",
        );
        embedded.push({ alt: img.alt ?? "", image: up.blob });
      }
      record.embed = { $type: "app.bsky.embed.images", images: embedded };
    }

    const created = await readJson<{ uri: string }>(
      await fetchImpl(`${service}/xrpc/com.atproto.repo.createRecord`, {
        method: "POST",
        headers: { ...auth, "Content-Type": "application/json" },
        body: JSON.stringify({ repo: session.did, collection: "app.bsky.feed.post", record }),
      }),
      "Bluesky post",
    );
    const rkey = created.uri.split("/").pop();
    return { externalId: created.uri, url: `https://bsky.app/profile/${session.handle}/post/${rkey}` };
  },
};
