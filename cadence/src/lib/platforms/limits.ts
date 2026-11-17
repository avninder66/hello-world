import type { MediaItem, Platform } from "../types";

export interface PlatformSpec {
  platform: Platform;
  label: string;
  maxChars: number;
  /** How characters are counted for the limit. */
  counting: "chars" | "graphemes" | "x";
  maxImages: number;
  allowsVideo: boolean;
  requiresMedia: boolean;
  connect: "oauth" | "credentials";
  notes: string;
}

export const PLATFORM_SPECS: Record<Platform, PlatformSpec> = {
  linkedin: {
    platform: "linkedin", label: "LinkedIn", maxChars: 3000, counting: "chars", maxImages: 1,
    allowsVideo: false, requiresMedia: false, connect: "oauth",
    notes: "Professional tone works best. First ~210 characters show before 'see more'.",
  },
  x: {
    platform: "x", label: "X", maxChars: 280, counting: "x", maxImages: 4,
    allowsVideo: false, requiresMedia: false, connect: "oauth",
    notes: "280 characters; every link counts as 23. Posting needs a paid X API tier.",
  },
  facebook: {
    platform: "facebook", label: "Facebook Page", maxChars: 63206, counting: "chars", maxImages: 10,
    allowsVideo: false, requiresMedia: false, connect: "oauth",
    notes: "Posts to a Facebook Page you manage. Shorter posts (<80 chars) get more engagement.",
  },
  instagram: {
    platform: "instagram", label: "Instagram", maxChars: 2200, counting: "chars", maxImages: 10,
    allowsVideo: true, requiresMedia: true, connect: "oauth",
    notes: "Needs at least one image or video (public URL). Links in captions are not clickable. Max 30 hashtags.",
  },
  threads: {
    platform: "threads", label: "Threads", maxChars: 500, counting: "chars", maxImages: 10,
    allowsVideo: true, requiresMedia: false, connect: "oauth",
    notes: "500 characters. Conversational tone.",
  },
  bluesky: {
    platform: "bluesky", label: "Bluesky", maxChars: 300, counting: "graphemes", maxImages: 4,
    allowsVideo: false, requiresMedia: false, connect: "credentials",
    notes: "300 characters. Links and hashtags become clickable automatically. Images max 1MB.",
  },
  mastodon: {
    platform: "mastodon", label: "Mastodon", maxChars: 500, counting: "chars", maxImages: 4,
    allowsVideo: true, requiresMedia: false, connect: "credentials",
    notes: "500 characters on most servers; each link counts as 23. Add image descriptions (alt text).",
  },
};

const URL_RE = /https?:\/\/[^\s<>"']+/g;

export function graphemeLength(text: string): number {
  const seg = new Intl.Segmenter(undefined, { granularity: "grapheme" });
  return Array.from(seg.segment(text)).length;
}

/** Character count as the platform measures it. */
export function countChars(platform: Platform, text: string): number {
  const spec = PLATFORM_SPECS[platform];
  if (spec.counting === "graphemes") return graphemeLength(text);
  if (spec.counting === "x" || platform === "mastodon") {
    // Links count as 23 characters regardless of length.
    const withoutUrls = text.replace(URL_RE, "x".repeat(23));
    return graphemeLength(withoutUrls);
  }
  return [...text].length;
}

export interface ValidationIssue {
  platform: Platform;
  message: string;
}

/** Returns problems that would stop this content publishing on the platform. */
export function validateForPlatform(platform: Platform, text: string, media: MediaItem[]): ValidationIssue[] {
  const spec = PLATFORM_SPECS[platform];
  const issues: ValidationIssue[] = [];
  const count = countChars(platform, text);
  if (count > spec.maxChars) {
    issues.push({ platform, message: `${spec.label}: ${count}/${spec.maxChars} characters — too long.` });
  }
  if (!text.trim() && media.length === 0) {
    issues.push({ platform, message: `${spec.label}: post is empty.` });
  }
  if (spec.requiresMedia && media.length === 0) {
    issues.push({ platform, message: `${spec.label}: needs at least one image or video.` });
  }
  const images = media.filter((m) => m.type === "image").length;
  const videos = media.filter((m) => m.type === "video").length;
  if (images > spec.maxImages) {
    issues.push({ platform, message: `${spec.label}: at most ${spec.maxImages} image(s).` });
  }
  if (videos > 0 && !spec.allowsVideo) {
    issues.push({ platform, message: `${spec.label}: video isn't supported yet.` });
  }
  if (platform === "instagram" && (text.match(/#[\p{L}\p{N}_]+/gu)?.length ?? 0) > 30) {
    issues.push({ platform, message: "Instagram: at most 30 hashtags." });
  }
  return issues;
}
