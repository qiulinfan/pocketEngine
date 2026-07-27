export type AgentEventKind =
  | "session_started"
  | "assistant_text_delta"
  | "assistant_message"
  | "plan_updated"
  | "tool_call_started"
  | "tool_call_completed"
  | "approval_requested"
  | "usage_updated"
  | "turn_completed"
  | "turn_failed"
  | "session_closed";

export interface AgentEvent {
  kind: AgentEventKind;
  session_id: string;
  turn_id?: string;
  item_id?: string;
  display_text?: string;
  structured_payload?: Record<string, unknown>;
}

export interface AgentProbeResult {
  provider: string;
  installed: boolean;
  authenticated: boolean;
  auth_status: string;
  version: string;
  supports_json_stream: boolean;
  supports_session_resume: boolean;
  supports_mcp: boolean;
  error?: string;
}

export interface AgentSessionConfig {
  project_root: string;
  system_context?: string;
  mcp_endpoint?: string;
  mcp_bearer_token?: string;
}

export interface AgentAdapter {
  readonly provider: string;

  probe(): Promise<AgentProbeResult>;
  startSession(config: AgentSessionConfig): Promise<string>;
  sendMessage(sessionId: string, message: string): Promise<void>;
  cancelTurn(sessionId: string): Promise<void>;
  closeSession(sessionId: string): Promise<void>;
}
