import { PlatformError, readJson, sleep, type FetchLike, type PublisherAdapter } from "./types";

const GRAPH = "https://graph.threads.net/v1.0";

async function post<T>(fetchImpl: FetchLike, path: string, params: Record<string, string>, label: string) {
  return readJson<T>(
    await fetchImpl(`${GRAPH}/${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(params),
    }),
    label,
  );
}

async function waitReady(fetchImpl: FetchLike, id: string, token: string) {
  for (let i = 0; i < 30; i++) {
    const r = await readJson<{ status?: string; error_message?: string }>(
      await fetchImpl(`${GRAPH}/${id}?${new URLSearchParams({ fields: "status,error_message", access_token: token })}`),
      "Threads container status",
    );
    if (r.status === "FINISHED" || r.status === "PUBLISHED") return;
    if (r.status === "ERROR" || r.status === "EXPIRED") {
      throw new PlatformError(`Threads media failed: ${r.error_message ?? r.status}`);
    }
    await sleep(2000);
  }
  throw new PlatformError("Threads media still processing; will retry", { retryable: true });
}

export const threads: PublisherAdapter = {
  platform: "threads",
  async publish({ account, text, media }, fetchImpl) {
    const token = account.accessToken;
    const uid = account.externalId;

    const container = async (item: (typeof media)[number] | null, carousel: boolean) => {
      const params: Record<string, string> = { access_token: token };
      if (!item) {
        params.media_type = "TEXT";
      } else if (item.type === "video") {
        params.media_type = "VIDEO";
        params.video_url = item.url;
      } else {
        params.media_type = "IMAGE";
        params.image_url = item.url;
        if (item.alt) params.alt_text = item.alt;
      }
      if (carousel) params.is_carousel_item = "true";
      else params.text = text;
      const r = await post<{ id: string }>(fetchImpl, `${uid}/threads`, params, "Threads container");
      if (item) await waitReady(fetchImpl, r.id, token);
      return r.id;
    };

    let creationId: string;
    if (media.length <= 1) {
      creationId = await container(media[0] ?? null, false);
    } else {
      const children: string[] = [];
      for (const item of media.slice(0, 10)) children.push(await container(item, true));
      const r = await post<{ id: string }>(
        fetchImpl, `${uid}/threads`,
        { media_type: "CAROUSEL", children: children.join(","), text, access_token: token },
        "Threads carousel",
      );
      creationId = r.id;
      await waitReady(fetchImpl, creationId, token);
    }

    const published = await post<{ id: string }>(
      fetchImpl, `${uid}/threads_publish`, { creation_id: creationId, access_token: token }, "Threads publish",
    );
    let url: string | undefined;
    try {
      const r = await readJson<{ permalink?: string }>(
        await fetchImpl(`${GRAPH}/${published.id}?${new URLSearchParams({ fields: "permalink", access_token: token })}`),
        "Threads permalink",
      );
      url = r.permalink;
    } catch {
      /* best effort */
    }
    return { externalId: published.id, url };
  },

  // Threads long-lived tokens last 60 days and can be refreshed while valid.
  async refresh(account, fetchImpl) {
    const r = await readJson<{ access_token: string; expires_in: number }>(
      await fetchImpl(
        `https://graph.threads.net/refresh_access_token?${new URLSearchParams({
          grant_type: "th_refresh_token",
          access_token: account.accessToken,
        })}`,
      ),
      "Threads token refresh",
    );
    return { accessToken: r.access_token, expiresAt: new Date(Date.now() + r.expires_in * 1000) };
  },
};
