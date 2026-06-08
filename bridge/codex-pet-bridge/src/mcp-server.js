#!/usr/bin/env node
const BRIDGE_URL = process.env.PET_BRIDGE_URL || "http://127.0.0.1:17366/events";

process.stdin.setEncoding("utf8");

let buffer = "";
process.stdin.on("data", (chunk) => {
  buffer += chunk;
  let newlineIndex;
  while ((newlineIndex = buffer.indexOf("\n")) >= 0) {
    const line = buffer.slice(0, newlineIndex).trim();
    buffer = buffer.slice(newlineIndex + 1);
    if (line) void handleLine(line);
  }
});

async function handleLine(line) {
  let message;
  try {
    message = JSON.parse(line);
  } catch {
    return;
  }

  if (!message.id) return;

  try {
    if (message.method === "initialize") {
      respond(message.id, {
        protocolVersion: message.params?.protocolVersion || "2024-11-05",
        capabilities: { tools: {} },
        serverInfo: { name: "codex-pet-bridge", version: "0.1.0" }
      });
      return;
    }

    if (message.method === "tools/list") {
      respond(message.id, {
        tools: [
          {
            name: "pet_emit_event",
            description: "Send a status or speech event to the local desktop pet bridge.",
            inputSchema: {
              type: "object",
              properties: {
                source: { type: "string", description: "Source app, such as claude-desktop, claude-code, codex, or custom." },
                status: { type: "string", description: "Short machine status, such as thinking, working, idle, needs-attention, error." },
                message: { type: "string", description: "Short text for the pet to display or react to." },
                notify: { type: "boolean", description: "Force this event into or out of the unread notification queue." },
                progress: { type: "number", description: "Optional progress from 0 to 1 for near-complete style updates." },
                workspace: { type: "string", description: "Optional workspace path or label." },
                sessionId: { type: "string", description: "Optional upstream session identifier." }
              },
              required: ["message"]
            }
          }
        ]
      });
      return;
    }

    if (message.method === "tools/call" && message.params?.name === "pet_emit_event") {
      const event = normalizeToolArgs(message.params.arguments || {});
      await postEvent(event);
      respond(message.id, {
        content: [{ type: "text", text: `Sent pet event: ${event.status} - ${event.message}` }],
        isError: false
      });
      return;
    }

    respond(message.id, null, { code: -32601, message: `Method not found: ${message.method}` });
  } catch (error) {
    respond(message.id, null, { code: -32000, message: error.message || String(error) });
  }
}

function normalizeToolArgs(args) {
  return {
    source: args.source || "mcp-client",
    type: "mcp-tool",
    status: args.status || "event",
    message: args.message || "Agent event",
    notify: typeof args.notify === "boolean" ? args.notify : undefined,
    progress: typeof args.progress === "number" ? args.progress : undefined,
    workspace: args.workspace || "",
    sessionId: args.sessionId || ""
  };
}

async function postEvent(event) {
  const response = await fetch(BRIDGE_URL, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(event)
  });

  if (!response.ok) {
    throw new Error(`bridge returned ${response.status}`);
  }
}

function respond(id, result, error) {
  const payload = error
    ? { jsonrpc: "2.0", id, error }
    : { jsonrpc: "2.0", id, result };
  process.stdout.write(`${JSON.stringify(payload)}\n`);
}
