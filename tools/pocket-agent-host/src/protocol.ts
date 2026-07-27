export const BRIDGE_PROTOCOL_VERSION = 1;

export type BridgeMessageType = "request" | "response" | "event";

export interface BridgeError {
  code: string;
  message: string;
}

export interface BridgeMessage {
  protocol_version: number;
  id: string;
  type: BridgeMessageType;
  method: string;
  payload: Record<string, unknown>;
  timestamp_ms?: number;
}

let nextMessageId = 1;

export function createMessage(
  type: BridgeMessageType,
  method: string,
  payload: Record<string, unknown>,
  id?: string,
): BridgeMessage {
  return {
    protocol_version: BRIDGE_PROTOCOL_VERSION,
    id: id ?? `host_${nextMessageId++}`,
    type,
    method,
    payload,
    timestamp_ms: Date.now(),
  };
}

export function createResponse(
  request: BridgeMessage,
  payload: Record<string, unknown>,
): BridgeMessage {
  return createMessage("response", request.method, { ok: true, ...payload }, request.id);
}

export function createErrorResponse(
  request: BridgeMessage,
  code: string,
  message: string,
): BridgeMessage {
  return createMessage(
    "response",
    request.method,
    { ok: false, error: { code, message } satisfies BridgeError },
    request.id,
  );
}

export function parseBridgeMessage(line: string): BridgeMessage {
  let value: unknown;
  try {
    value = JSON.parse(line);
  } catch (error) {
    throw new Error(`Invalid JSON: ${String(error)}`);
  }

  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new Error("Bridge message must be a JSON object.");
  }

  const candidate = value as Partial<BridgeMessage>;
  if (candidate.protocol_version !== BRIDGE_PROTOCOL_VERSION) {
    throw new Error(`Unsupported protocol version: ${String(candidate.protocol_version)}`);
  }
  if (candidate.type !== "request" && candidate.type !== "response" && candidate.type !== "event") {
    throw new Error(`Invalid message type: ${String(candidate.type)}`);
  }
  if (typeof candidate.id !== "string" || candidate.id.length === 0) {
    throw new Error("Bridge message id must be a non-empty string.");
  }
  if (typeof candidate.method !== "string" || candidate.method.length === 0) {
    throw new Error("Bridge message method must be a non-empty string.");
  }
  if (typeof candidate.payload !== "object" || candidate.payload === null || Array.isArray(candidate.payload)) {
    throw new Error("Bridge message payload must be a JSON object.");
  }

  return candidate as BridgeMessage;
}

export function serializeBridgeMessage(message: BridgeMessage): string {
  return JSON.stringify(message);
}
