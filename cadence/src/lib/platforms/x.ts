import { downloadMedia, readJson, type PublisherAdapter } from "./types";

const API = "https://api.x.com/2";

export const xAdapter: PublisherAdapter = {
  platform: "x",
  async publish({ account, text, media }, fetchImpl) {
    const mediaIds: string[] = [];
    for (const item of media.filter((m) => m.type === "image").slice(0, 4)) {
      const { bytes } = await downloadMedia(item.url, fetchImpl);
      const form = new FormData();
      form.append("media", bytes);
      form.append("media_category", "tweet_image");
      const up = await readJson<{ data: { id: string } }>(
        await fetchImpl(`${API}/media/upload`, {
          method: "POST",
          headers: { Authorization: `Bearer ${account.accessToken}` },
          body: form,
        }),
        "X media upload",
      );
      mediaIds.push(up.data.id);
      if (item.alt) {
        await fetchImpl(`${API}/media/metadata`, {
          method: "POST",
          headers: { Authorization: `Bearer ${account.accessToken}`, "Content-Type": "application/json" },
          body: JSON.stringify({ id: up.data.id, metadata: { alt_text: { text: item.alt.slice(0, 1000) } } }),
        });
      }
    }

    const body: Record<string, unknown> = { text };
    if (mediaIds.length) body.media = { media_ids: mediaIds };
    const json = await readJson<{ data: { id: string } }>(
      await fetchImpl(`${API}/tweets`, {
        method: "POST",
        headers: { Authorization: `Bearer ${account.accessToken}`, "Content-Type": "application/json" },
        body: JSON.stringify(body),
      }),
      "X post",
    );
    const handle = account.handle ?? "i";
    return { externalId: json.data.id, url: `https://x.com/${handle}/status/${json.data.id}` };
  },

  async refresh(account, fetchImpl) {
    if (!account.refreshToken) throw new Error("X account has no refresh token; reconnect it.");
    const id = process.env.X_CLIENT_ID!;
    const secret = process.env.X_CLIENT_SECRET!;
    const json = await readJson<{ access_token: string; refresh_token?: string; expires_in: number }>(
      await fetchImpl(`${API}/oauth2/token`, {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          Authorization: `Basic ${Buffer.from(`${id}:${secret}`).toString("base64")}`,
        },
        body: new URLSearchParams({ grant_type: "refresh_token", refresh_token: account.refreshToken, client_id: id }),
      }),
      "X token refresh",
    );
    return {
      accessToken: json.access_token,
      refreshToken: json.refresh_token ?? account.refreshToken,
      expiresAt: new Date(Date.now() + json.expires_in * 1000),
    };
  },
};
