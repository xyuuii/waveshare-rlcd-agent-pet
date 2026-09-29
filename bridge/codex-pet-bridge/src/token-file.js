import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

// Where the bridge token lives when it is not passed in the environment.
// Keeping it in one 0600 file means LaunchAgents, hooks and the menu bar app
// can all authenticate without copying the secret into several configs.
export const DEFAULT_TOKEN_FILE = join(homedir(), ".codex-pet-bridge", "token");

// Clients (hooks, notify, plugins) fall back to the default file so they work
// without env plumbing. The server only reads a file when told to via
// PET_BRIDGE_TOKEN_FILE, so a plain `npm start` keeps its old loopback behaviour.
export function resolveBridgeToken({ env = process.env, allowDefaultFile = true } = {}) {
  const direct = typeof env.PET_BRIDGE_TOKEN === "string" ? env.PET_BRIDGE_TOKEN.trim() : "";
  if (direct) return direct;
  const configured = typeof env.PET_BRIDGE_TOKEN_FILE === "string" ? env.PET_BRIDGE_TOKEN_FILE.trim() : "";
  const file = configured || (allowDefaultFile ? DEFAULT_TOKEN_FILE : "");
  if (!file) return "";
  try {
    return readFileSync(file, "utf8").trim();
  } catch {
    return "";
  }
}
