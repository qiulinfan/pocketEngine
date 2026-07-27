import { randomBytes, randomUUID, timingSafeEqual } from "node:crypto";
import type { Server as HttpServer } from "node:http";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { createMcpExpressApp } from "@modelcontextprotocol/sdk/server/express.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import type { NextFunction, Request, Response } from "express";
import * as z from "zod";

export interface McpToolExecutor {
  (tool: string, argumentsValue: Record<string, unknown>): Promise<unknown>;
}

export interface PocketMcpInfo {
  endpoint: string;
  bearerToken: string;
}

function secureTokenEquals(actual: string | undefined, expected: string): boolean {
  if (actual === undefined || !actual.startsWith("Bearer ")) return false;
  const actualToken = Buffer.from(actual.slice(7));
  const expectedToken = Buffer.from(expected);
  return actualToken.length === expectedToken.length && timingSafeEqual(actualToken, expectedToken);
}

function isAllowedOrigin(origin: string | undefined): boolean {
  if (origin === undefined || origin === "null") return true;
  try {
    const url = new URL(origin);
    return url.protocol === "http:" &&
      (url.hostname === "127.0.0.1" ||
       url.hostname === "localhost" ||
       url.hostname === "::1");
  } catch {
    return false;
  }
}

function createServer(execute: McpToolExecutor): McpServer {
  const server = new McpServer(
    { name: "pocketengine", version: "0.1.0" },
    {
      capabilities: { tools: {} },
      instructions:
        "PocketEngine read-only editor context. Inspect editor state before scene details. " +
        "These tools never mutate files, SceneDocument, or runtime state.",
    },
  );
  const register = (
    name: string,
    description: string,
    inputSchema: Record<string, z.ZodTypeAny>,
  ): void => {
    server.registerTool(
      name,
      {
        description,
        inputSchema,
        annotations: {
          readOnlyHint: true,
          destructiveHint: false,
          idempotentHint: true,
          openWorldHint: false,
        },
      },
      async (args) => {
        try {
          const result = await execute(name, args);
          return {
            content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
            structuredContent:
              typeof result === "object" && result !== null && !Array.isArray(result)
                ? result as Record<string, unknown>
                : { result },
          };
        } catch (error) {
          return {
            isError: true,
            content: [{
              type: "text" as const,
              text: error instanceof Error ? error.message : String(error),
            }],
          };
        }
      },
    );
  };
  register("get_editor_state", "Return current project, scene, mode, selection, and dirty state.", {});
  register("get_current_scene", "Summarize authoring actors from the current SceneDocument.", {
    include_components: z.boolean().optional().default(true),
    max_actors: z.number().int().min(1).max(500).optional().default(100),
  });
  register("inspect_actor", "Inspect one authoring actor by stable scene UID.", {
    actor_uid: z.number().int().nonnegative(),
  });
  register("list_component_types", "List registered component types and default properties.", {
    query: z.string().optional().default(""),
    limit: z.number().int().min(1).max(500).optional().default(100),
  });
  register("search_assets", "Search project assets without reading outside the project root.", {
    query: z.string().optional().default(""),
    type: z.string().optional().default(""),
    limit: z.number().int().min(1).max(200).optional().default(50),
  });
  return server;
}

export class PocketMcpServer {
  private httpServer?: HttpServer;
  private readonly transports = new Set<StreamableHTTPServerTransport>();
  readonly bearerToken = randomBytes(32).toString("base64url");
  endpoint = "";

  constructor(private readonly execute: McpToolExecutor) {}

  async start(): Promise<PocketMcpInfo> {
    if (this.httpServer !== undefined) {
      return { endpoint: this.endpoint, bearerToken: this.bearerToken };
    }
    const app = createMcpExpressApp({ host: "127.0.0.1" });
    app.use((req: Request, res: Response, next: NextFunction) => {
      const origin = req.headers.origin;
      if (!isAllowedOrigin(origin)) {
        res.status(403).json({ error: "MCP_INVALID_ORIGIN" });
        return;
      }
      if (!secureTokenEquals(req.headers.authorization, this.bearerToken)) {
        res.status(401).set("WWW-Authenticate", "Bearer").json({ error: "MCP_UNAUTHORIZED" });
        return;
      }
      next();
    });
    app.post("/mcp", async (req: Request, res: Response) => {
      const transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: undefined,
        enableJsonResponse: true,
      });
      this.transports.add(transport);
      const server = createServer(this.execute);
      try {
        await server.connect(transport);
        await transport.handleRequest(req, res, req.body);
      } finally {
        this.transports.delete(transport);
        await transport.close();
        await server.close();
      }
    });
    app.get("/mcp", (_req: Request, res: Response) => res.status(405).set("Allow", "POST").end());
    app.delete("/mcp", (_req: Request, res: Response) => res.status(405).set("Allow", "POST").end());
    const listeningServer = await new Promise<HttpServer>((resolve, reject) => {
      const server = app.listen(0, "127.0.0.1");
      server.once("listening", () => resolve(server));
      server.once("error", reject);
    });
    this.httpServer = listeningServer;
    const address = listeningServer.address();
    if (address === undefined || address === null || typeof address === "string") {
      throw new Error("MCP server has no TCP address.");
    }
    this.endpoint = `http://127.0.0.1:${address.port}/mcp`;
    return { endpoint: this.endpoint, bearerToken: this.bearerToken };
  }

  async close(): Promise<void> {
    for (const transport of this.transports) await transport.close();
    this.transports.clear();
    const server = this.httpServer;
    this.httpServer = undefined;
    if (server !== undefined) {
      await new Promise<void>((resolve, reject) => {
        server.close((error) => error === undefined ? resolve() : reject(error));
        server.closeAllConnections();
      });
    }
  }
}
