import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { dirname, resolve } from "node:path";
import { createInterface } from "node:readline";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  createMessage,
  parseBridgeMessage,
  serializeBridgeMessage,
  type BridgeMessage,
} from "../protocol.js";

const currentDirectory = dirname(fileURLToPath(import.meta.url));
const hostEntry = resolve(currentDirectory, "../index.js");

function waitForMessage(
  messages: BridgeMessage[],
  predicate: (message: BridgeMessage) => boolean,
  timeoutMs = 2000,
): Promise<BridgeMessage> {
  return new Promise((resolveMessage, reject) => {
    const started = Date.now();
    const poll = (): void => {
      const match = messages.find(predicate);
      if (match !== undefined) {
        resolveMessage(match);
        return;
      }
      if (Date.now() - started >= timeoutMs) {
        reject(new Error("Timed out waiting for sidecar message."));
        return;
      }
      setTimeout(poll, 10);
    };
    poll();
  });
}

test("sidecar runs a complete Fake Agent turn over JSONL", async () => {
  const child = spawn(process.execPath, [hostEntry], {
    stdio: ["pipe", "pipe", "pipe"],
  });
  const messages: BridgeMessage[] = [];
  const reader = createInterface({ input: child.stdout });
  reader.on("line", (line) => messages.push(parseBridgeMessage(line)));

  const send = (message: BridgeMessage): void => {
    child.stdin.write(`${serializeBridgeMessage(message)}\n`);
  };

  try {
    send(createMessage("request", "host.initialize", { project_root: "Projects/Default" }, "test_1"));
    await waitForMessage(messages, (message) => message.method === "host.ready");

    send(createMessage("request", "agent.start_session", { provider: "fake" }, "test_2"));
    const started = await waitForMessage(
      messages,
      (message) =>
        message.method === "agent.event" &&
        (message.payload.event as { kind?: string } | undefined)?.kind === "session_started",
    );
    assert.equal(started.type, "event");

    send(createMessage("request", "agent.send_message", { message: "bridge test" }, "test_3"));
    const completed = await waitForMessage(
      messages,
      (message) =>
        message.method === "agent.event" &&
        (message.payload.event as { kind?: string } | undefined)?.kind === "turn_completed",
    );
    assert.equal(completed.type, "event");
    assert.ok(
      messages.some(
        (message) =>
          message.method === "agent.event" &&
          (message.payload.event as { kind?: string } | undefined)?.kind === "assistant_text_delta",
      ),
    );
  } finally {
    send(createMessage("request", "host.shutdown", {}, "test_shutdown"));
    await new Promise<void>((resolveExit) => child.once("exit", () => resolveExit()));
    reader.close();
  }
});
