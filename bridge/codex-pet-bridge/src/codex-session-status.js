export function inferCodexRealtimeStateFromLines(lines) {
  for (let index = lines.length - 1; index >= 0; index -= 1) {
    const state = inferCodexRealtimeStateFromLine(lines[index]);
    if (state) return state;
  }
  return null;
}

export function inferCodexRealtimeStateFromLine(line) {
  if (!line || !line.trim()) return null;

  let item;
  try {
    item = JSON.parse(line);
  } catch {
    return null;
  }

  const payload = item?.payload && typeof item.payload === "object" ? item.payload : {};
  const time = typeof item.timestamp === "string" ? item.timestamp : "";

  if (item.type === "event_msg") {
    switch (payload.type) {
      case "task_complete":
      case "turn_aborted":
        return { status: "completed", time };
      case "task_started":
      case "user_message":
      case "agent_message":
        return { status: "thinking", time };
      default:
        return null;
    }
  }

  if (item.type !== "response_item") return null;

  switch (payload.type) {
    case "reasoning":
    case "function_call_output":
      return { status: "thinking", time };
    case "web_search_call":
      return { status: "searching", time };
    case "function_call":
      if (payload.name === "request_user_input") {
        return { status: "needs-attention", time };
      }
      return { status: "tool-use", time };
    case "message":
      if (payload.role === "user" || payload.role === "assistant") {
        return { status: "thinking", time };
      }
      return null;
    default:
      return null;
  }
}

/**
 * Pick the best current-state view from two sources.
 *
 * Prefers `codexRealtime` (finer-grained JSONL-derived state) when it is
 * at least as recent as the bridge event. On timestamp tie, realtime wins
 * because it carries more detail than agent-sync "running"/"completed".
 */
export function pickCurrentState(eventCurrent, codexRealtime) {
  const realtimeTime = Date.parse(codexRealtime?.time || 0);
  const eventTime = Date.parse(eventCurrent?.time || 0);
  const eventFamily = sourceFamily(eventCurrent?.source);
  const realtimeFamily = sourceFamily(codexRealtime?.source);

  if (codexRealtime && (!eventFamily || eventFamily === realtimeFamily) && realtimeTime >= eventTime) {
    return codexRealtime;
  }

  if (eventCurrent) {
    return {
      source: eventCurrent.source || "",
      task: eventCurrent.task || "",
      status: eventCurrent.status || "idle",
      time: eventCurrent.time || ""
    };
  }

  return null;
}

function sourceFamily(source) {
  const text = String(source || "").toLowerCase();
  if (text.includes("codex")) return "codex";
  if (text.includes("claude")) return "claude";
  if (text.includes("hermes")) return "hermes";
  if (text.includes("openclaw")) return "openclaw";
  return "";
}
