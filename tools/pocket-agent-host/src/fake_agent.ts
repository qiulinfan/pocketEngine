import type {
  AgentAdapter,
  AgentEvent,
  AgentProbeResult,
  AgentSessionConfig,
} from "./agent.js";

export type AgentEventSink = (event: AgentEvent) => void;

interface FakeSession {
  id: string;
  nextTurn: number;
  cancelled: boolean;
  closed: boolean;
}

export class FakeAgentAdapter implements AgentAdapter {
  readonly provider = "fake";

  private nextSession = 1;
  private readonly sessions = new Map<string, FakeSession>();

  constructor(
    private readonly emit: AgentEventSink,
    private readonly chunkDelayMs = 25,
  ) {}

  async probe(): Promise<AgentProbeResult> {
    return {
      provider: this.provider,
      installed: true,
      authenticated: true,
      auth_status: "offline_fixture",
      version: "phase0",
      supports_json_stream: true,
      supports_session_resume: true,
      supports_mcp: false,
    };
  }

  async startSession(_config: AgentSessionConfig): Promise<string> {
    const sessionId = `fake_session_${this.nextSession++}`;
    this.sessions.set(sessionId, {
      id: sessionId,
      nextTurn: 1,
      cancelled: false,
      closed: false,
    });
    this.emit({
      kind: "session_started",
      session_id: sessionId,
      display_text: "Fake Agent session started.",
    });
    return sessionId;
  }

  async sendMessage(sessionId: string, message: string): Promise<void> {
    const session = this.requireSession(sessionId);
    session.cancelled = false;
    const turnId = `fake_turn_${session.nextTurn++}`;
    const response = `Fake Agent received: ${message}`;
    const chunks = this.chunkText(response, 8);

    this.emit({
      kind: "plan_updated",
      session_id: sessionId,
      turn_id: turnId,
      display_text: "Phase 0: echo the request through the normalized event stream.",
    });

    for (const chunk of chunks) {
      await this.delay(this.chunkDelayMs);
      if (session.cancelled) {
        this.emit({
          kind: "turn_failed",
          session_id: sessionId,
          turn_id: turnId,
          display_text: "Fake Agent turn cancelled.",
          structured_payload: { code: "AGENT_TURN_CANCELLED" },
        });
        return;
      }
      this.emit({
        kind: "assistant_text_delta",
        session_id: sessionId,
        turn_id: turnId,
        display_text: chunk,
      });
    }

    this.emit({
      kind: "assistant_message",
      session_id: sessionId,
      turn_id: turnId,
      display_text: response,
    });
    this.emit({
      kind: "turn_completed",
      session_id: sessionId,
      turn_id: turnId,
      display_text: "Fake Agent turn completed.",
    });
  }

  async cancelTurn(sessionId: string): Promise<void> {
    this.requireSession(sessionId).cancelled = true;
  }

  async closeSession(sessionId: string): Promise<void> {
    const session = this.requireSession(sessionId);
    session.cancelled = true;
    session.closed = true;
    this.sessions.delete(sessionId);
    this.emit({
      kind: "session_closed",
      session_id: sessionId,
      display_text: "Fake Agent session closed.",
    });
  }

  private requireSession(sessionId: string): FakeSession {
    const session = this.sessions.get(sessionId);
    if (session === undefined || session.closed) {
      throw new Error(`Unknown fake session: ${sessionId}`);
    }
    return session;
  }

  private chunkText(value: string, chunkSize: number): string[] {
    const chunks: string[] = [];
    for (let offset = 0; offset < value.length; offset += chunkSize) {
      chunks.push(value.slice(offset, offset + chunkSize));
    }
    return chunks;
  }

  private async delay(milliseconds: number): Promise<void> {
    if (milliseconds <= 0) return;
    await new Promise<void>((resolve) => setTimeout(resolve, milliseconds));
  }
}
