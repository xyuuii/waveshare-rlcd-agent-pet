import { execFileSync } from "node:child_process";

const DEFAULT_TAIL_LINES = 500;
const DEFAULT_ACTIVE_WINDOW_MS = 45000;
const DEFAULT_COMPLETED_WINDOW_MS = 30 * 60 * 1000;

export function readHermesLogActivity(logPath, options = {}) {
  if (!logPath) return null;
  let text = "";
  try {
    text = execFileSync("/usr/bin/tail", ["-n", String(options.tailLines || DEFAULT_TAIL_LINES), logPath], {
      encoding: "utf8",
      timeout: 1000
    });
  } catch {
    return null;
  }
  return parseHermesLogActivity(text, options);
}

export function parseHermesLogActivity(text, options = {}) {
  const nowMs = Number.isFinite(options.nowMs) ? options.nowMs : Date.now();
  const activeWindowMs = Number(options.activeWindowMs || DEFAULT_ACTIVE_WINDOW_MS);
  const completedWindowMs = Number(options.completedWindowMs || DEFAULT_COMPLETED_WINDOW_MS);
  const sessions = new Map();
  let latest = null;

  for (const line of String(text || "").split("\n")) {
    const timestampMs = parseHermesTimestamp(line);
    if (!Number.isFinite(timestampMs)) continue;

    const turn = parseTurnStart(line);
    if (turn) {
      const record = sessions.get(turn.sessionId) || { sessionId: turn.sessionId };
      record.startedAtMs = timestampMs;
      record.task = turn.task || record.task || "hermes-session";
      record.lastEventMs = timestampMs;
      sessions.set(turn.sessionId, record);
      latest = newerRecord(latest, record);
      continue;
    }

    const ended = parseTurnEnd(line);
    if (ended) {
      const record = sessions.get(ended.sessionId) || { sessionId: ended.sessionId };
      record.endedAtMs = timestampMs;
      record.completedKey = `${ended.sessionId}:${timestampMs}`;
      record.task = record.task || "hermes-session";
      record.lastEventMs = timestampMs;
      sessions.set(ended.sessionId, record);
      latest = newerRecord(latest, record);
    }
  }

  if (!latest) return null;

  const active = latest.startedAtMs > (latest.endedAtMs || 0) && nowMs - latest.startedAtMs <= activeWindowMs;
  const recentlyCompleted =
    latest.endedAtMs > 0 && latest.endedAtMs >= (latest.startedAtMs || 0) && nowMs - latest.endedAtMs <= completedWindowMs;

  if (!active && !recentlyCompleted) return null;

  return {
    source: "hermes",
    task: latest.task || "hermes-session",
    sessionId: latest.sessionId || "",
    active,
    completed: recentlyCompleted && !active,
    completedKey: latest.completedKey || "",
    startedAtMs: latest.startedAtMs || 0,
    endedAtMs: latest.endedAtMs || 0
  };
}

function parseHermesTimestamp(line) {
  const match = String(line || "").match(/^(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2}),(\d{3})/);
  if (!match) return NaN;
  return Date.parse(`${match[1]}T${match[2]}.${match[3]}`);
}

function parseTurnStart(line) {
  if (!line.includes("agent.turn_context: conversation turn:")) return null;
  const sessionId = valueAfter(line, "session=");
  if (!sessionId) return null;
  return {
    sessionId,
    task: quotedValueAfter(line, "msg=") || "hermes-session"
  };
}

function parseTurnEnd(line) {
  if (!line.includes("agent.conversation_loop: Turn ended:")) return null;
  const sessionId = valueAfter(line, "session=");
  return sessionId ? { sessionId } : null;
}

function valueAfter(line, marker) {
  const index = line.indexOf(marker);
  if (index < 0) return "";
  return line.slice(index + marker.length).split(/\s+/)[0]?.replace(/[,)]$/, "") || "";
}

function quotedValueAfter(line, marker) {
  const index = line.indexOf(marker);
  if (index < 0) return "";
  const rest = line.slice(index + marker.length);
  if (!rest.startsWith("'")) return valueAfter(line, marker);
  const end = rest.indexOf("'", 1);
  return end > 1 ? rest.slice(1, end) : "";
}

function newerRecord(left, right) {
  if (!left) return right;
  return (right.lastEventMs || 0) >= (left.lastEventMs || 0) ? right : left;
}
