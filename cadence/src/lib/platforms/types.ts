import type { MediaItem, Platform } from "../types";

/** Decrypted credentials and metadata the adapter needs to publish. */
export interface PublishAccount {
  id: string;
  platform: Platform;
  externalId: string;
  handle: string | null;
  accessToken: string;
  refreshToken: string | null;
  tokenExpiresAt: Date | null;
  meta: Record<string, unknown>;
}

export interface PublishRequest {
  account: PublishAccount;
  text: string;
  media: MediaItem[];
  /** Stable id for this target, used as an idempotency key where supported. */
  idempotencyKey: string;
}

export interface PublishResult {
  externalId: string;
  url?: string;
}

export interface TokenSet {
  accessToken: string;
  refreshToken?: string | null;
  expiresAt?: Date | null;
}

export type FetchLike = typeof fetch;

export interface PublisherAdapter {
  platform: Platform;
  publish(req: PublishRequest, fetchImpl: FetchLike): Promise<PublishResult>;
  /** Exchanges a refresh token (or long-lived token) for a fresh access token. */
  refresh?(account: PublishAccount, fetchImpl: FetchLike): Promise<TokenSet>;
}

/**
 * Error raised by adapters. `retryable` failures (rate limits, 5xx) are
 * retried with backoff; `needsReauth` marks the account for reconnection.
 */
export class PlatformError extends Error {
  retryable: boolean;
  needsReauth: boolean;
  status?: number;
  constructor(message: string, opts: { retryable?: boolean; needsReauth?: boolean; status?: number } = {}) {
    super(message);
    this.name = "PlatformError";
    this.retryable = opts.retryable ?? false;
    this.needsReauth = opts.needsReauth ?? false;
    this.status = opts.status;
  }
}

/** Throws a PlatformError describing a failed HTTP response. */
export async function assertOk(res: Response, label: string): Promise<void> {
  if (res.ok) return;
  let body = "";
  try {
    body = (await res.text()).slice(0, 500);
  } catch {
    /* ignore */
  }
  throw new PlatformError(`${label} failed (${res.status}): ${body || res.statusText}`, {
    status: res.status,
    retryable: res.status === 429 || res.status >= 500,
    needsReauth: res.status === 401,
  });
}

export async function readJson<T>(res: Response, label: string): Promise<T> {
  await assertOk(res, label);
  return (await res.json()) as T;
}

/** Downloads a media file so it can be uploaded to platforms that need bytes. */
export async function downloadMedia(url: string, fetchImpl: FetchLike): Promise<{ bytes: Blob; contentType: string }> {
  const res = await fetchImpl(url);
  await assertOk(res, `Downloading media ${url}`);
  const contentType = res.headers.get("content-type") ?? "application/octet-stream";
  return { bytes: await res.blob(), contentType };
}

export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
