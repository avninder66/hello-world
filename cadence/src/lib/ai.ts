import "server-only";
import Anthropic from "@anthropic-ai/sdk";
import { betaZodOutputFormat } from "@anthropic-ai/sdk/helpers/beta/zod";
import { z } from "zod";
import { PLATFORM_SPECS } from "./platforms/limits";
import type { Platform, Workspace } from "./types";

export const AI_MODEL = process.env.CADENCE_AI_MODEL ?? "claude-opus-5-5";

const DraftsSchema = z.object({
  drafts: z.array(
    z.object({
      platform: z.enum(["linkedin", "x", "facebook", "instagram", "threads", "bluesky", "mastodon"]),
      content: z.string(),
      notes: z.string().describe("One short sentence on the angle taken or anything the reviewer should check."),
    }),
  ),
});
export type Drafts = z.infer<typeof DraftsSchema>;

export class AiUnavailableError extends Error {}

let client: Anthropic | null = null;
function anthropic() {
  if (!process.env.ANTHROPIC_API_KEY && !process.env.ANTHROPIC_AUTH_TOKEN) {
    throw new AiUnavailableError("Set ANTHROPIC_API_KEY to use AI drafting.");
  }
  client ??= new Anthropic();
  return client;
}

function systemPrompt(ws: Workspace) {
  return [
    `You write social media posts for "${ws.name}". A person reviews every draft before it is published.`,
    ws.brand_voice.trim()
      ? `Brand voice and guidelines for this client:\n<brand_voice>\n${ws.brand_voice.trim()}\n</brand_voice>`
      : "No brand guidelines were provided: write in a clear, friendly, professional voice.",
    "Write one draft per requested platform, adapted to how people use that platform, and stay within its character limit:",
    ...Object.values(PLATFORM_SPECS).map((s) => `- ${s.platform}: max ${s.maxChars} characters. ${s.notes}`),
    "Do not invent facts, statistics, prices, dates, quotes or links that aren't in the brief. If the brief lacks something a post needs, write around it and say so in notes.",
    "Return plain text posts (no markdown). Use hashtags sparingly and only where the platform suits them.",
  ].join("\n\n");
}

/** Drafts platform-specific versions of a post from a short brief. */
export async function draftPosts(ws: Workspace, brief: string, platforms: Platform[]): Promise<Drafts> {
  const unique = [...new Set(platforms)];
  const response = await anthropic().beta.messages.parse({
    model: AI_MODEL,
    max_tokens: 16000,
    betas: ["server-side-fallback-2026-07-01"],
    fallbacks: "default",
    output_config: { effort: "medium", format: betaZodOutputFormat(DraftsSchema) },
    system: systemPrompt(ws),
    messages: [
      {
        role: "user",
        content: `Platforms: ${unique.join(", ")}\n\nBrief:\n${brief}`,
      },
    ],
  });
  if (response.stop_reason === "refusal") {
    throw new Error("Claude declined to write this post. Try rephrasing the brief.");
  }
  if (!response.parsed_output) throw new Error("Claude's reply couldn't be read; please try again.");
  return response.parsed_output;
}
