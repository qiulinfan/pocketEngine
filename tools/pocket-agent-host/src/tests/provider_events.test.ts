import assert from "node:assert/strict";
import test from "node:test";
import { normalizeClaudeEvent } from "../claude_adapter.js";
import { normalizeCodexEvent } from "../codex_adapter.js";

test("normalizes Codex JSONL agent and MCP events", () => {
  const session = "codex_session";
  const turn = "turn_1";
  const toolStart = normalizeCodexEvent({
    type: "item.started",
    item: { id: "item_1", type: "mcp_tool_call", tool: "get_current_scene" },
  }, session, turn);
  const message = normalizeCodexEvent({
    type: "item.completed",
    item: { id: "item_2", type: "agent_message", text: "The scene has three actors." },
  }, session, turn);
  const completed = normalizeCodexEvent({
    type: "turn.completed",
    usage: { input_tokens: 20, output_tokens: 8 },
  }, session, turn);

  assert.equal(toolStart[0]?.kind, "tool_call_started");
  assert.equal(message[0]?.kind, "assistant_message");
  assert.equal(message[0]?.display_text, "The scene has three actors.");
  assert.deepEqual(completed.map((event) => event.kind), ["usage_updated", "turn_completed"]);
});

test("normalizes Claude stream-json messages and results", () => {
  const assistant = normalizeClaudeEvent({
    type: "assistant",
    message: {
      id: "msg_1",
      content: [
        { type: "tool_use", id: "tool_1", name: "mcp__pocketengine__inspect_actor", input: {} },
        { type: "text", text: "The actor owns a Transform." },
      ],
    },
  }, "claude_session", "turn_1");
  const result = normalizeClaudeEvent({
    type: "result",
    subtype: "success",
    is_error: false,
    total_cost_usd: 0.001,
    num_turns: 2,
  }, "claude_session", "turn_1");

  assert.deepEqual(assistant.map((event) => event.kind), [
    "tool_call_started",
    "assistant_message",
  ]);
  assert.equal(assistant[1]?.display_text, "The actor owns a Transform.");
  assert.deepEqual(result.map((event) => event.kind), ["usage_updated", "turn_completed"]);
});
