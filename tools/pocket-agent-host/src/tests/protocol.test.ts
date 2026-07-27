import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import {
  BRIDGE_PROTOCOL_VERSION,
  createResponse,
  parseBridgeMessage,
  serializeBridgeMessage,
} from "../protocol.js";

const currentDirectory = dirname(fileURLToPath(import.meta.url));
const fixturePath = resolve(currentDirectory, "../../test/fixtures/host-initialize.request.jsonl");

test("parses the protocol v1 initialize fixture", async () => {
  const line = (await readFile(fixturePath, "utf8")).trim();
  const message = parseBridgeMessage(line);
  assert.equal(message.protocol_version, BRIDGE_PROTOCOL_VERSION);
  assert.equal(message.type, "request");
  assert.equal(message.method, "host.initialize");
  assert.equal(message.payload.project_root, "Projects/Default");
});

test("response keeps the request id and round trips", () => {
  const request = parseBridgeMessage(
    '{"protocol_version":1,"id":"editor_7","type":"request","method":"agent.probe","payload":{}}',
  );
  const response = createResponse(request, { agents: [] });
  const parsed = parseBridgeMessage(serializeBridgeMessage(response));
  assert.equal(parsed.id, "editor_7");
  assert.equal(parsed.type, "response");
  assert.equal(parsed.payload.ok, true);
});

test("rejects unsupported protocol versions", () => {
  assert.throws(
    () => parseBridgeMessage('{"protocol_version":2,"id":"x","type":"request","method":"x","payload":{}}'),
    /Unsupported protocol version/,
  );
});
