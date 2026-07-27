import assert from "node:assert/strict";
import test from "node:test";
import type { AgentEvent } from "../agent.js";
import { FakeAgentAdapter } from "../fake_agent.js";

test("fake agent emits a normalized streaming turn", async () => {
  const events: AgentEvent[] = [];
  const adapter = new FakeAgentAdapter((event) => events.push(event), 0);
  const sessionId = await adapter.startSession({ project_root: "Projects/Default" });
  await adapter.sendMessage(sessionId, "hello");

  assert.equal(events[0]?.kind, "session_started");
  assert.ok(events.some((event) => event.kind === "assistant_text_delta"));
  assert.equal(events.at(-2)?.kind, "assistant_message");
  assert.equal(events.at(-1)?.kind, "turn_completed");
  assert.equal(events.at(-2)?.display_text, "Fake Agent received: hello");
});
