import { PlatformError, readJson, sleep, type FetchLike, type PublisherAdapter } from "./types";

export const GRAPH_VERSION = "v23.0";
const GRAPH = `https://graph.facebook.com/${GRAPH_VERSION}`;

async function graphPost<T>(fetchImpl: FetchLike, path: string, params: Record<string, string>, label: string) {
  return readJson<T>(
    await fetchImpl(`${GRAPH}/${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(params),
    }),
    label,
  );
}

async function graphGet<T>(fetchImpl: FetchLike, path: string, params: Record<string, string>, label: string) {
  return readJson<T>(await fetchImpl(`${GRAPH}/${path}?${new URLSearchParams(params)}`), label);
}

export const facebook: PublisherAdapter = {
  platform: "facebook",
  async publish({ account, text, media }, fetchImpl) {
    const token = account.accessToken; // page access token
    const pageId = account.externalId;
    const images = media.filter((m) => m.type === "image");

    let postId: string;
    if (images.length === 1) {
      const r = await graphPost<{ id: string; post_id?: string }>(
        fetchImpl, `${pageId}/photos`, { url: images[0].url, caption: text, access_token: token }, "Facebook photo post",
      );
      postId = r.post_id ?? r.id;
    } else {
      const params: Record<string, string> = { message: text, access_token: token };
      // Multiple photos: upload unpublished, then attach to one feed post.
      for (const [i, img] of images.entries()) {
        const r = await graphPost<{ id: string }>(
          fetchImpl, `${pageId}/photos`, { url: img.url, published: "false", access_token: token }, "Facebook photo upload",
        );
        params[`attached_media[${i}]`] = JSON.stringify({ media_fbid: r.id });
      }
      const r = await graphPost<{ id: string }>(fetchImpl, `${pageId}/feed`, params, "Facebook post");
      postId = r.id;
    }
    return { externalId: postId, url: `https://www.facebook.com/${postId}` };
  },
};

/** Waits for an Instagram/Threads media container to finish processing. */
async function waitForContainer(fetchImpl: FetchLike, base: string, id: string, token: string, field: string) {
  for (let i = 0; i < 30; i++) {
    const r = await readJson<Record<string, string>>(
      await fetchImpl(`${base}/${id}?${new URLSearchParams({ fields: field, access_token: token })}`),
      "Checking media container",
    );
    const status = r[field];
    if (status === "FINISHED" || status === "PUBLISHED") return;
    if (status === "ERROR" || status === "EXPIRED") {
      throw new PlatformError(`Media container ${id} failed to process (${status})`);
    }
    await sleep(2000);
  }
  throw new PlatformError("Media is still processing; will retry", { retryable: true });
}

export const instagram: PublisherAdapter = {
  platform: "instagram",
  async publish({ account, text, media }, fetchImpl) {
    const token = account.accessToken; // page token of the linked Facebook Page
    const igId = account.externalId;
    if (media.length === 0) throw new PlatformError("Instagram posts need an image or video");

    const createItem = async (item: (typeof media)[number], carousel: boolean) => {
      const params: Record<string, string> = { access_token: token };
      if (item.type === "video") {
        params.media_type = carousel ? "VIDEO" : "REELS";
        params.video_url = item.url;
      } else {
        params.image_url = item.url;
        if (item.alt) params.alt_text = item.alt;
      }
      if (carousel) params.is_carousel_item = "true";
      else params.caption = text;
      const r = await graphPost<{ id: string }>(fetchImpl, `${igId}/media`, params, "Instagram media container");
      if (item.type === "video") await waitForContainer(fetchImpl, GRAPH, r.id, token, "status_code");
      return r.id;
    };

    let creationId: string;
    if (media.length === 1) {
      creationId = await createItem(media[0], false);
    } else {
      const children: string[] = [];
      for (const item of media.slice(0, 10)) children.push(await createItem(item, true));
      const r = await graphPost<{ id: string }>(
        fetchImpl, `${igId}/media`,
        { media_type: "CAROUSEL", children: children.join(","), caption: text, access_token: token },
        "Instagram carousel container",
      );
      creationId = r.id;
      await waitForContainer(fetchImpl, GRAPH, creationId, token, "status_code");
    }

    const published = await graphPost<{ id: string }>(
      fetchImpl, `${igId}/media_publish`, { creation_id: creationId, access_token: token }, "Instagram publish",
    );
    let url: string | undefined;
    try {
      url = (await graphGet<{ permalink?: string }>(fetchImpl, published.id, { fields: "permalink", access_token: token }, "Instagram permalink")).permalink;
    } catch {
      /* permalink is best effort */
    }
    return { externalId: published.id, url };
  },
};
