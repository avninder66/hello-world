import { downloadMedia, PlatformError, readJson, sleep, type FetchLike, type PublisherAdapter } from "./types";

export function normaliseInstance(url: string): string {
  const withScheme = /^https?:\/\//.test(url) ? url : `https://${url}`;
  const u = new URL(withScheme);
  if (u.protocol !== "https:") throw new Error("Mastodon instance must use https");
  return u.origin;
}

export async function verifyMastodon(instance: string, token: string, fetchImpl: FetchLike) {
  return readJson<{ id: string; username: string; acct: string; display_name: string; avatar: string; url: string }>(
    await fetchImpl(`${instance}/api/v1/accounts/verify_credentials`, { headers: { Authorization: `Bearer ${token}` } }),
    "Mastodon verify",
  );
}

export const mastodon: PublisherAdapter = {
  platform: "mastodon",
  async publish({ account, text, media, idempotencyKey }, fetchImpl) {
    const instance = normaliseInstance(String(account.meta.instance ?? ""));
    const auth = { Authorization: `Bearer ${account.accessToken}` };

    const mediaIds: string[] = [];
    for (const item of media.slice(0, 4)) {
      const { bytes } = await downloadMedia(item.url, fetchImpl);
      const form = new FormData();
      form.append("file", bytes, item.url.split("/").pop() ?? "upload");
      if (item.alt) form.append("description", item.alt);
      const res = await fetchImpl(`${instance}/api/v2/media`, { method: "POST", headers: auth, body: form });
      const att = await readJson<{ id: string; url: string | null }>(res, "Mastodon media upload");
      // 202 = still processing; wait until it has a URL.
      if (res.status === 202 || !att.url) {
        let ready = false;
        for (let i = 0; i < 30 && !ready; i++) {
          await sleep(2000);
          const check = await fetchImpl(`${instance}/api/v1/media/${att.id}`, { headers: auth });
          ready = check.status === 200;
        }
        if (!ready) throw new PlatformError("Mastodon media still processing; will retry", { retryable: true });
      }
      mediaIds.push(att.id);
    }

    const status = await readJson<{ id: string; url: string }>(
      await fetchImpl(`${instance}/api/v1/statuses`, {
        method: "POST",
        headers: { ...auth, "Content-Type": "application/json", "Idempotency-Key": idempotencyKey },
        body: JSON.stringify({ status: text, media_ids: mediaIds, visibility: "public" }),
      }),
      "Mastodon post",
    );
    return { externalId: status.id, url: status.url };
  },
};
