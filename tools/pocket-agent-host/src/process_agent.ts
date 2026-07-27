import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";

export interface JsonLineProcessOptions {
  command: string;
  args: string[];
  cwd: string;
  env?: NodeJS.ProcessEnv;
  stdin?: string;
  onJson: (value: Record<string, unknown>) => void;
}

export interface RunningJsonLineProcess {
  child: ChildProcessWithoutNullStreams;
  completion: Promise<void>;
}

export function runJsonLineProcess(options: JsonLineProcessOptions): RunningJsonLineProcess {
  const child = spawn(options.command, options.args, {
    cwd: options.cwd,
    env: options.env ?? process.env,
    stdio: ["pipe", "pipe", "pipe"],
  });
  let stderr = "";
  child.stderr.setEncoding("utf8");
  child.stderr.on("data", (chunk: string) => {
    stderr += chunk;
    if (stderr.length > 16_384) stderr = stderr.slice(-16_384);
  });

  const reader = createInterface({ input: child.stdout, crlfDelay: Infinity });
  reader.on("line", (line) => {
    const trimmed = line.trim();
    if (trimmed.length === 0) return;
    try {
      const value: unknown = JSON.parse(trimmed);
      if (typeof value === "object" && value !== null && !Array.isArray(value)) {
        options.onJson(value as Record<string, unknown>);
      }
    } catch {
      // Vendor stderr/stdout sometimes contains diagnostics. Only JSON objects
      // are part of the adapter contract.
    }
  });

  if (options.stdin !== undefined) child.stdin.end(options.stdin);
  else child.stdin.end();

  const completion = new Promise<void>((resolve, reject) => {
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      reader.close();
      if (code === 0) {
        resolve();
        return;
      }
      const detail = stderr.trim();
      reject(
        new Error(
          detail.length > 0
            ? detail
            : `${options.command} exited with ${signal ?? `code ${String(code)}`}.`,
        ),
      );
    });
  });
  return { child, completion };
}

export function terminateProcess(child: ChildProcessWithoutNullStreams | undefined): void {
  if (child === undefined || child.killed || child.exitCode !== null) return;
  child.kill("SIGTERM");
  setTimeout(() => {
    if (!child.killed && child.exitCode === null) child.kill("SIGKILL");
  }, 1_500).unref();
}

export async function probeCommand(
  provider: string,
  command: string,
  versionArgs: string[],
  capabilities: {
    supports_json_stream: boolean;
    supports_session_resume: boolean;
    supports_mcp: boolean;
  },
): Promise<import("./agent.js").AgentProbeResult> {
  return await new Promise((resolve) => {
    const child = spawn(command, versionArgs, { stdio: ["ignore", "pipe", "pipe"] });
    let output = "";
    let errorOutput = "";
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      output += chunk;
    });
    child.stderr.on("data", (chunk: string) => {
      errorOutput += chunk;
    });
    child.once("error", (error) => {
      resolve({
        provider,
        installed: false,
        authenticated: false,
        auth_status: "not_installed",
        version: "",
        ...capabilities,
        error: error.message,
      });
    });
    child.once("exit", (code) => {
      const version =
        output.trim().split(/\r?\n/).filter(Boolean).at(-1) ??
        errorOutput.trim().split(/\r?\n/).filter(Boolean).at(-1) ??
        "";
      resolve({
        provider,
        installed: code === 0,
        authenticated: false,
        auth_status: code === 0 ? "unknown" : "not_installed",
        version: code === 0 ? version : "",
        ...capabilities,
        error: code === 0 ? undefined : (errorOutput.trim() || `${command} exited with code ${String(code)}.`),
      });
    });
  });
}

export async function captureCommand(
  command: string,
  args: string[],
): Promise<{ ok: boolean; stdout: string; stderr: string }> {
  return await new Promise((resolve) => {
    const child = spawn(command, args, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => { stdout += chunk; });
    child.stderr.on("data", (chunk: string) => { stderr += chunk; });
    child.once("error", (error) => {
      resolve({ ok: false, stdout, stderr: error.message });
    });
    child.once("exit", (code) => {
      resolve({ ok: code === 0, stdout, stderr });
    });
  });
}
