import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname } from "node:path";

import { isAcceptablePosixTz } from "./system-timezone.js";

// Mirrors ClockStyle / ScreenPage names in the firmware (clock_model.cpp, bridge_client.cpp).
export const CLOCK_STYLES = ["sans", "segment", "dots", "analog", "words", "terminal", "pet"];
export const PAGES = ["overview", "usage", "clock"];
export const ANIM_ID_PATTERN = /^[a-z0-9][a-z0-9_-]{0,47}$/;

export function defaultSettings() {
  return {
    rev: 0,
    pageRev: 0,
    clockStyle: "sans",
    hour12: false,
    showSeconds: true,
    page: "",
    tzOverride: "",
    defaultAnimation: ""
  };
}

// Returns a new settings object; `rev` only moves when something the board
// displays changed, so the board can tell new instructions from old ones.
// A page change is one-shot: it is sent only with the revision that set it.
export function applySettingsPatch(current, patch, { now = Date.now() } = {}) {
  const next = { ...defaultSettings(), ...current };
  const errors = [];
  let displayChanged = false;
  let pageChanged = false;
  if (!patch || typeof patch !== "object") return { settings: next, changed: false, errors: ["patch must be an object"] };

  if ("clockStyle" in patch) {
    if (CLOCK_STYLES.includes(patch.clockStyle)) {
      displayChanged ||= next.clockStyle !== patch.clockStyle;
      next.clockStyle = patch.clockStyle;
    } else errors.push(`unknown clockStyle ${JSON.stringify(patch.clockStyle)}`);
  }
  for (const key of ["hour12", "showSeconds"]) {
    if (key in patch) {
      if (typeof patch[key] === "boolean") {
        displayChanged ||= next[key] !== patch[key];
        next[key] = patch[key];
      } else errors.push(`${key} must be boolean`);
    }
  }
  if ("page" in patch) {
    if (PAGES.includes(patch.page)) {
      next.page = patch.page;
      pageChanged = true;
      displayChanged = true;
    } else errors.push(`unknown page ${JSON.stringify(patch.page)}`);
  }
  if ("tzOverride" in patch) {
    if (patch.tzOverride === "" || isAcceptablePosixTz(patch.tzOverride)) next.tzOverride = patch.tzOverride;
    else errors.push("tzOverride must be a POSIX TZ rule such as GMT0BST,M3.5.0/1,M10.5.0");
  }
  if ("defaultAnimation" in patch) {
    if (patch.defaultAnimation === "" || ANIM_ID_PATTERN.test(patch.defaultAnimation)) {
      next.defaultAnimation = patch.defaultAnimation;
    } else errors.push("defaultAnimation must be an animation id");
  }
  if (displayChanged) {
    // Seconds since epoch keep revisions increasing across bridge restarts.
    next.rev = Math.max(next.rev + 1, Math.floor(now / 1000));
    if (pageChanged) next.pageRev = next.rev;
  }
  const changed = JSON.stringify(next) !== JSON.stringify({ ...defaultSettings(), ...current });
  return { settings: next, changed, errors };
}

export function displayCommand(settings) {
  if (!settings || !settings.rev) return null;
  const command = {
    rev: settings.rev,
    clock_style: settings.clockStyle,
    hour12: settings.hour12,
    show_seconds: settings.showSeconds
  };
  if (settings.page && settings.pageRev === settings.rev) command.page = settings.page;
  return command;
}

export class SettingsStore {
  constructor(path) {
    this.path = path;
    this.settings = defaultSettings();
  }

  async load() {
    try {
      const saved = JSON.parse(await readFile(this.path, "utf8"));
      this.settings = { ...defaultSettings(), ...saved };
    } catch {
      this.settings = defaultSettings();
    }
    return this.settings;
  }

  get() {
    return { ...this.settings };
  }

  async update(patch, options) {
    const result = applySettingsPatch(this.settings, patch, options);
    if (result.errors.length === 0 && result.changed) {
      this.settings = result.settings;
      await mkdir(dirname(this.path), { recursive: true });
      const tmp = `${this.path}.${process.pid}.tmp`;
      await writeFile(tmp, `${JSON.stringify(this.settings, null, 2)}\n`, "utf8");
      await rename(tmp, this.path);
    }
    return { ...result, settings: this.get() };
  }
}
