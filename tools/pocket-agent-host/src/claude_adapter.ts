import { randomUUID } from "node:crypto";
import type { ChildProcessWithoutNullStreams } from "node:child_process";
import type {
  AgentAdapter,
  AgentEvent,
  AgentProbeResult,
  AgentSessionConfig,
} from "./agent.js";
import type { AgentEventSink } from "./fake_agent.js";
import {
  captureCommand,
  probeCommand,
  runJsonLineProcess,
  terminateProcess,
} from "./process_agent.js";

interface ClaudeSession {
  id: string;
  config: AgentSessionConfig;
  vendorSessionId?: string;
  running?: ChildProcessWithoutNullStreams;
  closed: boolean;
  nextTurn: number;
}

function record(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value !== null && !Array.isArray(value)
    ? value as Record<string, unknown>
    : undefined;
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

export function normalizeClaudeEvent(
  raw: Record<string, unknown>,
  sessionId: string,
  turnId: string,
): AgentEvent[] {
  const type = text(raw.type);
  if (type === "assistant") {
    const message = record(raw.message);
    const content = Array.isArray(message?.content) ? message.content : [];
    const events: AgentEvent[] = [];
    for (const blockValue of content) {
      const block = record(blockValue);
      if (block?.type === "text") {
        events.push({
          kind: "assistant_message",
          session_id: sessionId,
          turn_id: turnId,
          item_id: text(message?.id),
          display_text: text(block.text),
        });
      } else if (block?.type === "tool_use") {
        events.push({
          kind: "tool_call_started",
          session_id: sessionId,
          turn_id: turnId,
          item_id: text(block.id),
          display_text: text(block.name),
          structured_payload: block,
        });
      }
    }
    return events;
  }
  if (type === "user") {
    const message = record(raw.message);
    const content = Array.isArray(message?.content) ? message.content : [];
    return content.flatMap((blockValue): AgentEvent[] => {
      const block = record(blockValue);
      if (block?.type !== "tool_result") return [];
      return [{
        kind: "tool_call_completed",
        session_id: sessionId,
        turn_id: turnId,
        item_id: text(block.tool_use_id),
        structured_payload: block,
      }];
    });
  }
  if (type === "result") {
    if (raw.is_error === true || raw.subtype !== "success") {
      return [{
        kind: "turn_failed",
        session_id: sessionId,
        turn_id: turnId,
        display_text: text(raw.result) || "Claude Code turn failed.",
        structured_payload: raw,
      }];
    }
    return [
      {
        kind: "usage_updated",
        session_id: sessionId,
        turn_id: turnId,
        structured_payload: {
          total_cost_usd: raw.total_cost_usd,
          duration_ms: raw.duration_ms,
          num_turns: raw.num_turns,
        },
      },
      {
        kind: "turn_completed",
        session_id: sessionId,
        turn_id: turnId,
        display_text: "Claude Code turn completed.",
      },
    ];
  }
  return [];
}

export class ClaudeAdapter implements AgentAdapter {
  readonly provider = "claude";
  private readonly sessions = new Map<string, ClaudeSession>();

  constructor(
    private readonly emit: AgentEventSink,
    private readonly command = process.env.POCKET_CLAUDE_COMMAND || "claude",
  ) {}

  async probe(): Promise<AgentProbeResult> {
    const result = await probeCommand(this.provider, this.command, ["--version"], {
      supports_json_stream: true,
      supports_session_resume: true,
      supports_mcp: true,
    });
    if (!result.installed) return result;
    const auth = await captureCommand(this.command, ["auth", "status", "--json"]);
    let status: Record<string, unknown> = {};
    try {
      status = JSON.parse(auth.stdout) as Record<string, unknown>;
    } catch {
      status = {};
    }
    result.authenticated = auth.ok && status.loggedIn === true;
    result.auth_status =
      result.authenticated
        ? `${String(status.authMethod ?? "logged_in")} (${String(status.apiProvider ?? "firstParty")})`
        : "not_authenticated";
    if (!result.authenticated) {
      result.error = auth.stderr.trim() || "Claude Code is installed but not authenticated.";
    }
    return result;
  }

  async startSession(config: AgentSessionConfig): Promise<string> {
    const id = `claude_${randomUUID()}`;
    this.sessions.set(id, { id, config, closed: false, nextTurn: 1 });
    this.emit({
      kind: "session_started",
      session_id: id,
      display_text: "Claude Code session started with MCP-only tools.",
    });
    return id;
  }

  async sendMessage(sessionId: string, message: string): Promise<void> {
    const session = this.requireSession(sessionId);
    if (session.running !== undefined) throw new Error("A Claude Code turn is already running.");
    const turnId = `claude_turn_${session.nextTurn++}`;
    const env = { ...process.env };
    if (session.config.mcp_bearer_token !== undefined) {
      env.POCKETENGINE_MCP_TOKEN =
        session.config.mcp_bearer_token;
    }
    const running = runJsonLineProcess({
      command: this.command,
      args: this.buildArgs(session, message),
      cwd: session.config.project_root,
      env,
      onJson: (raw) => {
        if (raw.type === "system" && raw.subtype === "init" && typeof raw.session_id === "string") {
          session.vendorSessionId = raw.session_id;
        }
        if (raw.type === "result" && typeof raw.session_id === "string") {
          session.vendorSessionId = raw.session_id;
        }
        for (const event of normalizeClaudeEvent(raw, session.id, turnId)) this.emit(event);
      },
    });
    session.running = running.child;
    try {
      await running.completion;
    } catch (error) {
      if (!session.closed) {
        this.emit({
          kind: "turn_failed",
          session_id: session.id,
          turn_id: turnId,
          display_text: error instanceof Error ? error.message : String(error),
          structured_payload: { code: "AGENT_PROCESS_FAILED" },
        });
      }
    } finally {
      session.running = undefined;
    }
  }

  async cancelTurn(sessionId: string): Promise<void> {
    terminateProcess(this.requireSession(sessionId).running);
  }

  async closeSession(sessionId: string): Promise<void> {
    const session = this.requireSession(sessionId);
    session.closed = true;
    terminateProcess(session.running);
    this.sessions.delete(sessionId);
    this.emit({
      kind: "session_closed",
      session_id: sessionId,
      display_text: "Claude Code session closed.",
    });
  }

  private buildArgs(session: ClaudeSession, message: string): string[] {
    const allowed = [
      "mcp__pocketengine__get_editor_state",
      "mcp__pocketengine__get_current_scene",
      "mcp__pocketengine__inspect_actor",
      "mcp__pocketengine__list_component_types",
      "mcp__pocketengine__search_assets",
    ].join(",");
    const mcpConfig = JSON.stringify({
      mcpServers: {
        pocketengine: {
          type: "http",
          url: session.config.mcp_endpoint,
          headers: {
            Authorization: "Bearer ${POCKETENGINE_MCP_TOKEN}",
          },
        },
      },
    });
    const args = [
      "--print",
      "--output-format", "stream-json",
      "--verbose",
      "--permission-mode", "dontAsk",
      "--tools", "",
      "--allowedTools", allowed,
      "--mcp-config", mcpConfig,
      "--strict-mcp-config",
    ];
    if (session.vendorSessionId !== undefined) args.push("--resume", session.vendorSessionId);
    const prompt = `${session.config.system_context ?? ""}\n\n${message}`.trim();
    args.push(prompt);
    return args;
  }

  private requireSession(sessionId: string): ClaudeSession {
    const session = this.sessions.get(sessionId);
    if (session === undefined || session.closed) throw new Error(`Unknown Claude session: ${sessionId}`);
    return session;
  }
}
