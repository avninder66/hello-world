import { WebStandardStreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/webStandardStreamableHttp.js";
import { authenticateApiKey } from "@/lib/auth";
import { buildMcpServer } from "@/lib/mcp/server";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

/**
 * Remote MCP endpoint (Streamable HTTP, stateless).
 * Connect with: claude mcp add --transport http cadence <APP_URL>/api/mcp --header "Authorization: Bearer cad_..."
 */
async function handle(request: Request): Promise<Response> {
  const auth = await authenticateApiKey(request);
  if (!auth) {
    return Response.json(
      { jsonrpc: "2.0", error: { code: -32001, message: "Missing or invalid API key. Create one in Cadence → API keys." }, id: null },
      { status: 401, headers: { "WWW-Authenticate": 'Bearer realm="cadence"' } },
    );
  }
  const server = buildMcpServer(auth);
  const transport = new WebStandardStreamableHTTPServerTransport({ sessionIdGenerator: undefined, enableJsonResponse: true });
  await server.connect(transport);
  try {
    return await transport.handleRequest(request);
  } finally {
    // Stateless: each request gets its own server instance.
    void server.close();
  }
}

export { handle as GET, handle as POST, handle as DELETE };
