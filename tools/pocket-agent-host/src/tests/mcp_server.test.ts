import assert from "node:assert/strict";
import test from "node:test";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { PocketMcpServer } from "../mcp_server.js";

test("MCP requires bearer auth and exposes only read-only tools", async () => {
  const calls: string[] = [];
  const server = new PocketMcpServer(async (tool, args) => {
    calls.push(tool);
    return { tool, args, writes_enabled: false };
  });
  const info = await server.start();
  const unauthorized = await fetch(info.endpoint, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize", params: {} }),
  });
  assert.equal(unauthorized.status, 401);
  const invalidOrigin = await fetch(info.endpoint, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${info.bearerToken}`,
      Origin: "http://localhost.example.com",
    },
    body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize", params: {} }),
  });
  assert.equal(invalidOrigin.status, 403);

  const client = new Client({ name: "pocketengine-test", version: "1.0.0" });
  const transport = new StreamableHTTPClientTransport(new URL(info.endpoint), {
    requestInit: {
      headers: { Authorization: `Bearer ${info.bearerToken}` },
    },
  });
  try {
    await client.connect(transport);
    const tools = await client.listTools();
    assert.deepEqual(
      tools.tools.map((tool) => tool.name).sort(),
      [
        "get_current_scene",
        "get_editor_state",
        "inspect_actor",
        "list_component_types",
        "search_assets",
      ],
    );
    assert.ok(tools.tools.every((tool) => tool.annotations?.readOnlyHint === true));
    const result = await client.callTool({ name: "get_editor_state", arguments: {} });
    assert.equal(result.isError, undefined);
    assert.deepEqual(calls, ["get_editor_state"]);
  } finally {
    await client.close();
    await server.close();
  }
});
