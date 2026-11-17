import "server-only";
import { z } from "zod";
import { AccessError, authenticateApiKey, type ApiKeyAuth } from "./auth";
import { PostValidationError } from "./posts";

/** Wraps a REST handler with API-key auth and consistent JSON errors. */
export function apiHandler<Ctx>(fn: (req: Request, auth: ApiKeyAuth, ctx: Ctx) => Promise<unknown>) {
  return async (req: Request, ctx: Ctx) => {
    const auth = await authenticateApiKey(req);
    if (!auth) return Response.json({ error: "Missing or invalid API key" }, { status: 401 });
    try {
      const result = await fn(req, auth, ctx);
      return result instanceof Response ? result : Response.json(result);
    } catch (err) {
      if (err instanceof PostValidationError) return Response.json({ error: err.message, issues: err.issues }, { status: 422 });
      if (err instanceof z.ZodError) return Response.json({ error: "Invalid input", issues: err.issues }, { status: 400 });
      if (err instanceof AccessError) return Response.json({ error: err.message }, { status: err.status });
      console.error(err);
      return Response.json({ error: "Internal error" }, { status: 500 });
    }
  };
}
