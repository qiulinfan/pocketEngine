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

interface CodexSession {
  id: string;
  config: AgentSessionConfig;
  vendorThreadId?: string;
  running?: ChildProcessWithoutNullStreams;
  closed: boolean;
  nextTurn: number;
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value !== null && !Array.isArray(value)
    ? value as Record<string, unknown>
    : undefined;
}

function asString(value: unknown): string {
  return typeof value === "string" ? value : "";
}

export function normalizeCodexEvent(
  raw: Record<string, unknown>,
  sessionId: string,
  turnId: string,
): AgentEvent[] {
  const type = asString(raw.type);
  const item = asRecord(raw.item);
  const itemType = asString(item?.type);
  const itemId = asString(item?.id);
  if (type === "item.completed" && itemType === "agent_message") {
    return [{
      kind: "assistant_message",
      session_id: sessionId,
      turn_id: turnId,
      item_id: itemId,
      display_text: asString(item?.text),
    }];
  }
  if (type === "item.completed" && itemType === "plan") {
    return [{
      kind: "plan_updated",
      session_id: sessionId,
      turn_id: turnId,
      item_id: itemId,
      display_text: asString(item?.text),
      structured_payload: raw,
    }];
  }
  if (type === "item.started" && itemType === "mcp_tool_call") {
    return [{
      kind: "tool_call_started",
      session_id: sessionId,
      turn_id: turnId,
      item_id: itemId,
      display_text: asString(item?.tool) || asString(item?.name),
      structured_payload: item,
    }];
  }
  if (type === "item.completed" && itemType === "mcp_tool_call") {
    return [{
      kind: "tool_call_completed",
      session_id: sessionId,
      turn_id: turnId,
      item_id: itemId,
      display_text: asString(item?.tool) || asString(item?.name),
      structured_payload: item,
    }];
  }
  if (type === "turn.completed") {
    return [
      {
        kind: "usage_updated",
        session_id: sessionId,
        turn_id: turnId,
        structured_payload: asRecord(raw.usage) ?? {},
      },
      {
        kind: "turn_completed",
        session_id: sessionId,
        turn_id: turnId,
        display_text: "Codex turn completed.",
      },
    ];
  }
  if (type === "turn.failed" || type === "error") {
    const error = asRecord(raw.error);
    return [{
      kind: "turn_failed",
      session_id: sessionId,
      turn_id: turnId,
      display_text: asString(error?.message) || asString(raw.message) || "Codex turn failed.",
      structured_payload: raw,
    }];
  }
  return [];
}

export class CodexAdapter implements AgentAdapter {
  readonly provider = "codex";
  private readonly sessions = new Map<string, CodexSession>();

  constructor(
    private readonly emit: AgentEventSink,
    private readonly command = process.env.POCKET_CODEX_COMMAND || "codex",
  ) {}

  async probe(): Promise<AgentProbeResult> {
    const result = await probeCommand(this.provider, this.command, ["--version"], {
      supports_json_stream: true,
      supports_session_resume: true,
      supports_mcp: true,
    });
    if (!result.installed) return result;
    const auth = await captureCommand(this.command, ["login", "status"]);
    result.authenticated = auth.ok;
    result.auth_status = auth.ok ? (auth.stdout.trim() || "logged_in") : "not_authenticated";
    if (!auth.ok) result.error = auth.stderr.trim() || "Codex is installed but not authenticated.";
    return result;
  }

  async startSession(config: AgentSessionConfig): Promise<string> {
    const id = `codex_${randomUUID()}`;
    this.sessions.set(id, { id, config, closed: false, nextTurn: 1 });
    this.emit({
      kind: "session_started",
      session_id: id,
      display_text: "Codex session started in read-only mode.",
    });
    return id;
  }

  async sendMessage(sessionId: string, message: string): Promise<void> {
    const session = this.requireSession(sessionId);
    if (session.running !== undefined) throw new Error("A Codex turn is already running.");
    const turnId = `codex_turn_${session.nextTurn++}`;
    const args = this.buildArgs(session, message);
    const env = { ...process.env };
    if (session.config.mcp_bearer_token !== undefined) {
      env.POCKETENGINE_MCP_TOKEN = session.config.mcp_bearer_token;
    }
    const running = runJsonLineProcess({
      command: this.command,
      args,
      cwd: session.config.project_root,
      env,
      onJson: (raw) => {
        if (raw.type === "thread.started" && typeof raw.thread_id === "string") {
          session.vendorThreadId = raw.thread_id;
        }
        for (const event of normalizeCodexEvent(raw, session.id, turnId)) this.emit(event);
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
      display_text: "Codex session closed.",
    });
  }

  private buildArgs(session: CodexSession, message: string): string[] {
    const mcp = session.config.mcp_endpoint;
    const configArgs = mcp === undefined
      ? []
      : [
          "-c", `mcp_servers.pocketengine.url=${JSON.stringify(mcp)}`,
          "-c", "mcp_servers.pocketengine.bearer_token_env_var=\"POCKETENGINE_MCP_TOKEN\"",
          "-c", "mcp_servers.pocketengine.required=true",
          "-c", "mcp_servers.pocketengine.enabled_tools=[\"get_editor_state\",\"get_current_scene\",\"inspect_actor\",\"list_component_types\",\"search_assets\"]",
        ];
    const prompt = `${session.config.system_context ?? ""}\n\n${message}`.trim();
    if (session.vendorThreadId !== undefined) {
      return [
        "exec", "resume", "--json", "--strict-config",
        ...configArgs,
        session.vendorThreadId,
        prompt,
      ];
    }
    return [
      "exec", "--json", "--color", "never", "--strict-config",
      "--sandbox", "read-only", "--cd", session.config.project_root,
      ...configArgs,
      prompt,
    ];
  }

  private requireSession(sessionId: string): CodexSession {
    const session = this.sessions.get(sessionId);
    if (session === undefined || session.closed) throw new Error(`Unknown Codex session: ${sessionId}`);
    return session;
  }
}
