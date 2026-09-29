// Reads Claude Code's own session transcripts (~/.claude/projects/<dir>/<id>.jsonl)
// to tell what Claude Code is doing, without hooks or any change to Claude's
// config. Hooks are still better for "waiting for your approval", which the
// transcript does not record.

import { open, readdir, stat } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";

export const DEFAULT_CLAUDE_PROJECTS_DIR = join(
  process.env.CLAUDE_CONFIG_DIR || join(homedir(), ".claude"),
  "projects"
);

const SEARCH_TOOLS = new Set(["grep", "glob", "websearch", "webfetch", "ls", "find", "search"]);
const TAIL_BYTES = 96 * 1024;

export function toolStatus(name) {
  const tool = String(name || "").toLowerCase();
  if (!tool) return "tool-use";
  if (SEARCH_TOOLS.has(tool) || tool.includes("search") || tool.includes("grep") || tool.includes("glob")) {
    return "searching";
  }
  return "tool-use";
}

function contentBlocks(message) {
  if (!message) return [];
  if (Array.isArray(message.content)) return message.content;
  if (typeof message.content === "string") return [{ type: "text", text: message.content }];
  return [];
}

// State from the newest meaningful transcript entry.
export function inferClaudeStateFromEntries(entries) {
  for (let index = entries.length - 1; index >= 0; index -= 1) {
    const entry = entries[index];
    if (!entry || typeof entry !== "object") continue;
    const time = typeof entry.timestamp === "string" ? entry.timestamp : "";
    if (entry.type === "assistant") {
      const message = entry.message || {};
      const blocks = contentBlocks(message);
      const toolUse = [...blocks].reverse().find((block) => block?.type === "tool_use");
      if (toolUse) return { status: toolStatus(toolUse.name), time, tool: String(toolUse.name || "") };
      if (message.stop_reason === "end_turn" || message.stop_reason === "stop_sequence") {
        return { status: "completed", time };
      }
      if (blocks.some((block) => block?.type === "thinking" || block?.type === "redacted_thinking")) {
        return { status: "thinking", time };
      }
      return { status: "working", time };
    }
    if (entry.type === "user") {
      const blocks = contentBlocks(entry.message);
      const text = blocks.filter((block) => block?.type === "text").map((block) => String(block.text || "")).join(" ");
      if (text.startsWith("[Request interrupted by user")) return { status: "completed", time };
      return { status: "thinking", time };
    }
  }
  return null;
}

export function contextFromUsage(usage, model = "") {
  if (!usage || typeof usage !== "object") return null;
  const used =
    Number(usage.input_tokens || 0) +
    Number(usage.cache_creation_input_tokens || 0) +
    Number(usage.cache_read_input_tokens || 0) +
    Number(usage.output_tokens || 0);
  if (!Number.isFinite(used) || used <= 0) return null;
  const bigWindow = /\[1m\]|1m-context|-1m\b/i.test(String(model)) || used > 200_000;
  return { used, window: bigWindow ? 1_000_000 : 200_000 };
}

export function shortModelName(model) {
  const text = String(model || "").toLowerCase();
  if (!text) return "";
  return text
    .replace(/^claude-/, "")
    .replace(/-\d{8}$/, "")
    .replace(/\[1m\]$/, " 1m")
    .toUpperCase();
}

function latestAssistant(entries) {
  for (let index = entries.length - 1; index >= 0; index -= 1) {
    const entry = entries[index];
    if (entry?.type === "assistant" && entry.message?.usage) return entry;
  }
  return null;
}

export function parseJsonLines(text, { skipFirstPartial = false } = {}) {
  const lines = text.split("\n");
  if (skipFirstPartial) lines.shift();
  const entries = [];
  for (const line of lines) {
    if (!line.trim()) continue;
    try {
      entries.push(JSON.parse(line));
    } catch {
      // partial line while Claude is writing
    }
  }
  return entries;
}

async function readTail(path, bytes = TAIL_BYTES) {
  const handle = await open(path, "r");
  try {
    const { size } = await handle.stat();
    const start = Math.max(0, size - bytes);
    const buffer = Buffer.alloc(size - start);
    await handle.read(buffer, 0, buffer.length, start);
    return { text: buffer.toString("utf8"), partial: start > 0 };
  } finally {
    await handle.close();
  }
}

export async function listTranscripts(projectsDir = DEFAULT_CLAUDE_PROJECTS_DIR) {
  const files = [];
  let projects = [];
  try {
    projects = await readdir(projectsDir, { withFileTypes: true });
  } catch {
    return files;
  }
  for (const project of projects) {
    if (!project.isDirectory()) continue;
    const dir = join(projectsDir, project.name);
    let names = [];
    try {
      names = await readdir(dir);
    } catch {
      continue;
    }
    for (const name of names) {
      if (!name.endsWith(".jsonl")) continue;
      const path = join(dir, name);
      try {
        const info = await stat(path);
        files.push({ path, mtimeMs: info.mtimeMs, size: info.size, sessionId: name.slice(0, -6) });
      } catch {
        // file vanished between readdir and stat
      }
    }
  }
  return files.sort((a, b) => b.mtimeMs - a.mtimeMs);
}

// Latest Claude Code session state, or null when nothing is recent enough.
export async function loadClaudeRealtimeState({
  projectsDir = DEFAULT_CLAUDE_PROJECTS_DIR,
  now = Date.now(),
  activeWindowMs = 3 * 60 * 1000,
  toolWindowMs = 15 * 60 * 1000,
  completedHoldMs = 20 * 1000,
  sourcePrefix = "local"
} = {}) {
  const [latest] = await listTranscripts(projectsDir);
  if (!latest || now - latest.mtimeMs > toolWindowMs) return null;
  let tail;
  try {
    tail = await readTail(latest.path);
  } catch {
    return null;
  }
  const entries = parseJsonLines(tail.text, { skipFirstPartial: tail.partial });
  const state = inferClaudeStateFromEntries(entries);
  if (!state?.time) return null;
  const ageMs = now - Date.parse(state.time);
  if (!Number.isFinite(ageMs) || ageMs < -60_000) return null;
  if (state.status === "completed" && ageMs > completedHoldMs) return null;
  const window = state.status === "tool-use" || state.status === "searching" ? toolWindowMs : activeWindowMs;
  if (state.status !== "completed" && ageMs > window) return null;
  const assistant = latestAssistant(entries);
  const model = String(assistant?.message?.model || "");
  const last = entries.at(-1) || {};
  return {
    source: `${sourcePrefix}-claude`,
    task: `${sourcePrefix}-claude-session`,
    status: state.status,
    time: state.time,
    tool: state.tool || "",
    sessionId: String(last.sessionId || latest.sessionId || ""),
    workspace: String(last.cwd || ""),
    model,
    context: contextFromUsage(assistant?.message?.usage, model)
  };
}

// Today's tokens across sessions, counted once per API message. Cached by
// file size so a busy transcript is only re-read from where it grew.
const todayCache = new Map();

function localDayKey(date) {
  return `${date.getFullYear()}-${date.getMonth() + 1}-${date.getDate()}`;
}

export async function claudeTokensToday({ projectsDir = DEFAULT_CLAUDE_PROJECTS_DIR, now = Date.now() } = {}) {
  const today = localDayKey(new Date(now));
  const startOfDay = new Date(now);
  startOfDay.setHours(0, 0, 0, 0);
  let total = 0;
  for (const file of await listTranscripts(projectsDir)) {
    if (file.mtimeMs < startOfDay.getTime()) break;  // sorted newest first
    let cached = todayCache.get(file.path);
    if (!cached || cached.day !== today || file.size < cached.offset) {
      cached = { day: today, offset: 0, tokens: 0, seen: new Set() };
      todayCache.set(file.path, cached);
    }
    if (file.size > cached.offset) {
      try {
        const handle = await open(file.path, "r");
        try {
          const buffer = Buffer.alloc(file.size - cached.offset);
          await handle.read(buffer, 0, buffer.length, cached.offset);
          const text = buffer.toString("utf8");
          const complete = text.lastIndexOf("\n") + 1;  // only whole lines
          for (const entry of parseJsonLines(text.slice(0, complete))) {
            if (entry?.type !== "assistant" || !entry.message?.usage) continue;
            if (localDayKey(new Date(entry.timestamp)) !== today) continue;
            const id = entry.message.id || entry.requestId || entry.uuid;
            if (id && cached.seen.has(id)) continue;
            if (id) cached.seen.add(id);
            const usage = entry.message.usage;
            cached.tokens +=
              Number(usage.input_tokens || 0) +
              Number(usage.cache_creation_input_tokens || 0) +
              Number(usage.output_tokens || 0);
          }
          cached.offset += Buffer.byteLength(text.slice(0, complete), "utf8");
        } finally {
          await handle.close();
        }
      } catch {
        // unreadable file: skip this round
      }
    }
    total += cached.tokens;
  }
  return total;
}
