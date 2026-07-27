import { createInterface } from "node:readline";
import { stdin, stdout } from "node:process";
import type { AgentAdapter, AgentEvent } from "./agent.js";
import { ClaudeAdapter } from "./claude_adapter.js";
import { CodexAdapter } from "./codex_adapter.js";
import { FakeAgentAdapter } from "./fake_agent.js";
import { PocketMcpServer } from "./mcp_server.js";
import {
  createErrorResponse,
  createMessage,
  createResponse,
  parseBridgeMessage,
  serializeBridgeMessage,
  type BridgeMessage,
} from "./protocol.js";

export class PocketAgentHost {
  private projectRoot = "";
  private initialized = false;
  private activeProvider = "";
  private activeSessionId = "";
  private readonly adapters = new Map<string, AgentAdapter>();
  private readonly mcpServer: PocketMcpServer;
  private mcpRequestSequence = 1;
  private readonly pendingMcpRequests = new Map<
    string,
    {
      resolve: (value: unknown) => void;
      reject: (error: Error) => void;
      timeout: NodeJS.Timeout;
    }
  >();

  constructor() {
    this.mcpServer = new PocketMcpServer((tool, argumentsValue) =>
      this.executeEditorTool(tool, argumentsValue));
    this.adapters.set("fake", new FakeAgentAdapter((event) => this.emitAgentEvent(event)));
    this.adapters.set("codex", new CodexAdapter((event) => this.emitAgentEvent(event)));
    this.adapters.set("claude", new ClaudeAdapter((event) => this.emitAgentEvent(event)));
  }

  run(): void {
    const reader = createInterface({ input: stdin, crlfDelay: Infinity });
    reader.on("line", (line) => {
      void this.handleLine(line);
    });
    reader.on("close", () => {
      void this.shutdown().finally(() => process.exit(0));
    });
  }

  private write(message: BridgeMessage): void {
    stdout.write(`${serializeBridgeMessage(message)}\n`);
  }

  private async handleLine(line: string): Promise<void> {
    let request: BridgeMessage;
    try {
      request = parseBridgeMessage(line);
    } catch (error) {
      this.write(
        createMessage("event", "host.error", {
          code: "HOST_PROTOCOL_INVALID_MESSAGE",
          message: error instanceof Error ? error.message : String(error),
        }),
      );
      return;
    }

    if (request.type !== "request") return;
    try {
      switch (request.method) {
        case "host.initialize":
          await this.initialize(request);
          break;
        case "host.shutdown":
          this.write(createResponse(request, {}));
          await this.shutdown();
          process.exit(0);
          break;
        case "agent.probe":
          await this.probe(request);
          break;
        case "agent.start_session":
          await this.startSession(request);
          break;
        case "agent.send_message":
          await this.sendMessage(request);
          break;
        case "agent.cancel_turn":
          await this.cancelTurn(request);
          break;
        case "agent.close_session":
          await this.closeSession(request);
          break;
        case "mcp.complete_request":
          this.completeMcpRequest(request);
          break;
        default:
          this.write(createErrorResponse(request, "HOST_METHOD_NOT_FOUND", `Unknown method: ${request.method}`));
      }
    } catch (error) {
      this.write(
        createErrorResponse(
          request,
          "HOST_REQUEST_FAILED",
          error instanceof Error ? error.message : String(error),
        ),
      );
    }
  }

  private async initialize(request: BridgeMessage): Promise<void> {
    const projectRoot = request.payload.project_root;
    if (typeof projectRoot !== "string" || projectRoot.length === 0) {
      this.write(createErrorResponse(request, "HOST_INVALID_PROJECT_ROOT", "project_root is required."));
      return;
    }
    this.projectRoot = projectRoot;
    const mcp = await this.mcpServer.start();
    this.initialized = true;
    const capabilities = [
      "jsonl_bridge",
      "agent_probe",
      "codex_adapter",
      "claude_adapter",
      "streamable_http_mcp",
      "read_only_scene_tools",
    ];
    this.write(createResponse(request, {
      capabilities,
      mcp_endpoint: mcp.endpoint,
    }));
    this.write(
      createMessage("event", "host.ready", {
        project_root: this.projectRoot,
        capabilities,
        mcp_endpoint: mcp.endpoint,
      }),
    );
  }

  private async probe(request: BridgeMessage): Promise<void> {
    const agents = await Promise.all([...this.adapters.values()].map((adapter) => adapter.probe()));
    this.write(createResponse(request, { agents }));
  }

  private async startSession(request: BridgeMessage): Promise<void> {
    this.requireInitialized();
    const provider = request.payload.provider;
    if (typeof provider !== "string") {
      this.write(createErrorResponse(request, "HOST_INVALID_PROVIDER", "provider is required."));
      return;
    }
    const adapter = this.adapters.get(provider);
    if (adapter === undefined) {
      this.write(createErrorResponse(request, "AGENT_NOT_INSTALLED", `Unknown provider: ${provider}`));
      return;
    }
    const probe = await adapter.probe();
    if (!probe.installed) {
      this.write(createErrorResponse(
        request,
        "AGENT_NOT_INSTALLED",
        probe.error ?? `${provider} is not installed.`,
      ));
      return;
    }
    if (!probe.authenticated) {
      this.write(createErrorResponse(
        request,
        "AGENT_AUTH_REQUIRED",
        probe.error ?? `${provider} requires authentication.`,
      ));
      return;
    }
    if (this.activeSessionId.length > 0) {
      await this.closeActiveSession();
    }
    this.activeProvider = provider;
    this.activeSessionId = await adapter.startSession({
      project_root: this.projectRoot,
      mcp_endpoint: this.mcpServer.endpoint,
      mcp_bearer_token: this.mcpServer.bearerToken,
      system_context:
        "You are connected to the PocketEngine editor. This Phase 1 session is strictly read-only. " +
        "Use the pocketengine MCP tools as the source of truth for the current editor, scene, actors, " +
        "components, and assets. Never edit files or attempt to mutate the scene.",
    });
    this.write(createResponse(request, { provider, session_id: this.activeSessionId }));
  }

  private async sendMessage(request: BridgeMessage): Promise<void> {
    const adapter = this.requireActiveAdapter();
    const message = request.payload.message;
    if (typeof message !== "string" || message.trim().length === 0) {
      this.write(createErrorResponse(request, "HOST_INVALID_MESSAGE", "message is required."));
      return;
    }
    this.write(createResponse(request, { session_id: this.activeSessionId }));
    void adapter.sendMessage(this.activeSessionId, message).catch((error: unknown) => {
      this.emitAgentEvent({
        kind: "turn_failed",
        session_id: this.activeSessionId,
        display_text: error instanceof Error ? error.message : String(error),
        structured_payload: { code: "AGENT_PROCESS_FAILED" },
      });
    });
  }

  private async cancelTurn(request: BridgeMessage): Promise<void> {
    await this.requireActiveAdapter().cancelTurn(this.activeSessionId);
    this.write(createResponse(request, { session_id: this.activeSessionId }));
  }

  private async closeSession(request: BridgeMessage): Promise<void> {
    await this.closeActiveSession();
    this.write(createResponse(request, {}));
  }

  private emitAgentEvent(event: AgentEvent): void {
    this.write(createMessage("event", "agent.event", { event }));
  }

  private requireInitialized(): void {
    if (!this.initialized) throw new Error("Host has not been initialized.");
  }

  private requireActiveAdapter(): AgentAdapter {
    if (this.activeSessionId.length === 0) throw new Error("No active agent session.");
    const adapter = this.adapters.get(this.activeProvider);
    if (adapter === undefined) throw new Error("Active agent adapter is unavailable.");
    return adapter;
  }

  private async closeActiveSession(): Promise<void> {
    if (this.activeSessionId.length === 0) return;
    const adapter = this.adapters.get(this.activeProvider);
    const sessionId = this.activeSessionId;
    this.activeSessionId = "";
    this.activeProvider = "";
    if (adapter !== undefined) await adapter.closeSession(sessionId);
  }

  private async shutdown(): Promise<void> {
    await this.closeActiveSession();
    for (const [requestId, pending] of this.pendingMcpRequests) {
      clearTimeout(pending.timeout);
      pending.reject(new Error("MCP_EDITOR_DISCONNECTED: editor bridge closed."));
      this.pendingMcpRequests.delete(requestId);
    }
    await this.mcpServer.close();
    this.initialized = false;
  }

  private executeEditorTool(
    tool: string,
    argumentsValue: Record<string, unknown>,
  ): Promise<unknown> {
    this.requireInitialized();
    const requestId = `mcp_${this.mcpRequestSequence++}`;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingMcpRequests.delete(requestId);
        reject(new Error(`MCP_TOOL_TIMEOUT: ${tool} did not complete within 10 seconds.`));
      }, 10_000);
      this.pendingMcpRequests.set(requestId, { resolve, reject, timeout });
      this.write(
        createMessage("request", "mcp.execute_request", {
          request_id: requestId,
          tool,
          arguments: argumentsValue,
        }),
      );
    });
  }

  private completeMcpRequest(request: BridgeMessage): void {
    const requestId = request.payload.request_id;
    if (typeof requestId !== "string") {
      this.write(createErrorResponse(request, "HOST_INVALID_MCP_REQUEST", "request_id is required."));
      return;
    }
    const pending = this.pendingMcpRequests.get(requestId);
    if (pending === undefined) {
      this.write(createErrorResponse(request, "MCP_REQUEST_NOT_FOUND", `Unknown request: ${requestId}`));
      return;
    }
    clearTimeout(pending.timeout);
    this.pendingMcpRequests.delete(requestId);
    if (request.payload.ok === true) {
      pending.resolve(request.payload.result);
    } else {
      const error = request.payload.error;
      const errorRecord =
        typeof error === "object" && error !== null && !Array.isArray(error)
          ? error as Record<string, unknown>
          : {};
      const code = typeof errorRecord.code === "string" ? errorRecord.code : "MCP_TOOL_FAILED";
      const message = typeof errorRecord.message === "string" ? errorRecord.message : "Editor tool failed.";
      pending.reject(new Error(`${code}: ${message}`));
    }
    this.write(createResponse(request, {}));
  }
}
