import { createServer } from "node:http";
import type { AddressInfo } from "node:net";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { Workspace } from "@/lib/types";

// A stand-in for the Claude API that records the request and returns a structured reply.
let lastBody: Record<string, unknown> = {};
let lastHeaders: Record<string, string | string[] | undefined> = {};
const server = createServer((req, res) => {
  let raw = "";
  req.on("data", (c) => (raw += c));
  req.on("end", () => {
    lastBody = JSON.parse(raw);
    lastHeaders = req.headers;
    const drafts = { drafts: [{ platform: "x", content: "Christmas menu is live 🎄", notes: "Kept it short." }] };
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({
      id: "msg_1", type: "message", role: "assistant", model: "claude-opus-5-5", stop_reason: "end_turn", stop_sequence: null,
      content: [{ type: "text", text: JSON.stringify(drafts) }],
      usage: { input_tokens: 10, output_tokens: 10 },
    }));
  });
});

beforeAll(async () => {
  await new Promise<void>((r) => server.listen(0, r));
  process.env.ANTHROPIC_BASE_URL = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
  process.env.ANTHROPIC_API_KEY = "test-key";
});
afterAll(() => server.close());

const ws: Workspace = {
  id: "w", name: "Acme Bakery", slug: "acme", timezone: "Europe/London", require_approval: true,
  brand_voice: "Warm, playful, British spelling.", created_at: "",
};

describe("AI drafting", () => {
  it("sends brand voice, structured output and refusal fallbacks, and parses drafts", async () => {
    const { draftPosts } = await import("@/lib/ai");
    const out = await draftPosts(ws, "Announce the Christmas menu", ["x", "x"]);
    expect(out.drafts[0]).toMatchObject({ platform: "x", content: "Christmas menu is live 🎄" });
    expect(lastBody.model).toBe("claude-opus-5-5");
    expect(lastBody.fallbacks).toBe("default");
    expect(String(lastHeaders["anthropic-beta"])).toContain("server-side-fallback-2026-07-01");
    expect(lastBody.output_config).toMatchObject({ effort: "medium", format: { type: "json_schema" } });
    expect(String(lastBody.system)).toContain("Warm, playful");
    expect(JSON.stringify(lastBody.messages)).toContain("Platforms: x\\n");
  });
});
