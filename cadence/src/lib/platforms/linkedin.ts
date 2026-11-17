import { assertOk, downloadMedia, PlatformError, readJson, type PublisherAdapter } from "./types";

export const LINKEDIN_VERSION = "202511";
const API = "https://api.linkedin.com/rest";

/**
 * LinkedIn "little text" format reserves these characters; they must be
 * escaped or the post is rejected/garbled. Hashtags become hashtag templates.
 */
export function toLinkedInCommentary(text: string): string {
  const escaped = text.replace(/[\\|{}@[\]()<>#*_~]/g, (c) => `\\${c}`);
  return escaped.replace(/\\#([\p{L}\p{N}_]+)/gu, (_m, tag: string) => `{hashtag|\\#|${tag}}`);
}

function headers(token: string): Record<string, string> {
  return {
    Authorization: `Bearer ${token}`,
    "LinkedIn-Version": LINKEDIN_VERSION,
    "X-Restli-Protocol-Version": "2.0.0",
    "Content-Type": "application/json",
  };
}

export const linkedin: PublisherAdapter = {
  platform: "linkedin",
  async publish({ account, text, media }, fetchImpl) {
    // meta.urn is "urn:li:person:<sub>" or "urn:li:organization:<id>"
    const author = String(account.meta.urn ?? `urn:li:person:${account.externalId}`);
    let content: Record<string, unknown> | undefined;

    const image = media.find((m) => m.type === "image");
    if (image) {
      const init = await readJson<{ value: { uploadUrl: string; image: string } }>(
        await fetchImpl(`${API}/images?action=initializeUpload`, {
          method: "POST",
          headers: headers(account.accessToken),
          body: JSON.stringify({ initializeUploadRequest: { owner: author } }),
        }),
        "LinkedIn image upload init",
      );
      const { bytes, contentType } = await downloadMedia(image.url, fetchImpl);
      const up = await fetchImpl(init.value.uploadUrl, {
        method: "PUT",
        headers: { Authorization: `Bearer ${account.accessToken}`, "Content-Type": contentType },
        body: bytes,
      });
      await assertOk(up, "LinkedIn image upload");
      content = { media: { id: init.value.image, altText: image.alt ?? "" } };
    }

    const res = await fetchImpl(`${API}/posts`, {
      method: "POST",
      headers: headers(account.accessToken),
      body: JSON.stringify({
        author,
        commentary: toLinkedInCommentary(text),
        visibility: "PUBLIC",
        distribution: { feedDistribution: "MAIN_FEED", targetEntities: [], thirdPartyDistributionChannels: [] },
        lifecycleState: "PUBLISHED",
        isReshareDisabledByAuthor: false,
        ...(content ? { content } : {}),
      }),
    });
    await assertOk(res, "LinkedIn post");
    const id = res.headers.get("x-restli-id");
    if (!id) throw new PlatformError("LinkedIn did not return a post id");
    return { externalId: id, url: `https://www.linkedin.com/feed/update/${id}/` };
  },
};
