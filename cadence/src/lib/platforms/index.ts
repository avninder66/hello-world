import type { Platform } from "../types";
import { bluesky } from "./bluesky";
import { linkedin } from "./linkedin";
import { mastodon } from "./mastodon";
import { facebook, instagram } from "./meta";
import { threads } from "./threads";
import type { PublisherAdapter } from "./types";
import { xAdapter } from "./x";

export const ADAPTERS: Record<Platform, PublisherAdapter> = {
  linkedin,
  x: xAdapter,
  facebook,
  instagram,
  threads,
  bluesky,
  mastodon,
};

export * from "./limits";
export * from "./types";
