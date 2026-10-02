#!/usr/bin/env node
// End-to-end check of the dashboard in a real browser (Playwright Chromium).
//
//   node tools/ui-e2e.mjs <video-file> [screenshot-dir]
//
// Starts a throwaway bridge (token on, temp dirs), fakes a board polling it,
// then drives /ui/: pick a clock face, convert the video, upload, play on the
// "board" and check what the board would receive. Needs `playwright`
// (PLAYWRIGHT_MODULE may point at its index.mjs) and a video the bundled
// Chromium can decode (VP9/WebM works everywhere).

import { spawn } from "node:child_process";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const video = process.argv[2];
const shots = process.argv[3] ? resolve(process.argv[3]) : "";
if (!video) {
  console.error("usage: node tools/ui-e2e.mjs <video-file> [screenshot-dir]");
  process.exit(2);
}
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || "playwright");

const TOKEN = "e2e-token-5a1b2c3d4e";
const PORT = 17800 + (process.pid % 150);
const BASE = `http://127.0.0.1:${PORT}`;
const root = await mkdtemp(join(tmpdir(), "pet-ui-e2e-"));
await writeFile(join(root, "token"), `${TOKEN}\n`, { mode: 0o600 });
if (shots) await mkdir(shots, { recursive: true });

const server = spawn(process.execPath, ["./src/bridge-server.js"], {
  cwd: new URL("..", import.meta.url).pathname,
  env: {
    ...process.env,
    PET_BRIDGE_TOKEN: "",
    PET_BRIDGE_TOKEN_FILE: join(root, "token"),
    PET_BRIDGE_PORT: String(PORT),
    PET_BRIDGE_LOG: join(root, "events.jsonl"),
    PET_BRIDGE_STATE: join(root, "bridge-state.json"),
    PET_BRIDGE_CLAUDE_PROJECTS: join(root, "claude-projects"),
    PET_BRIDGE_CODEX_STATE: join(root, "missing.sqlite"),
    PET_BRIDGE_EGG_DELAY_MS: "2500"
  },
  stdio: ["ignore", "inherit", "inherit"]
});

const failures = [];
function check(condition, message) {
  if (condition) console.log(`  ok   ${message}`);
  else {
    console.log(`  FAIL ${message}`);
    failures.push(message);
  }
}

async function api(path, init = {}) {
  const response = await fetch(`${BASE}${path}`, {
    ...init,
    headers: { authorization: `Bearer ${TOKEN}`, "content-type": "application/json", ...(init.headers || {}) }
  });
  return response.json();
}

let boardCounter = 0;
async function boardPoll() {
  const query = new URLSearchParams({
    token: TOKEN,
    focus: "auto",
    fw: "1.3.0",
    bat: "84",
    mv: "3980",
    temp: "22.6",
    hum: "48",
    rssi: "-52",
    up: String(3600 + boardCounter++),
    page: "clock",
    style: "segment",
    srev: "0",
    heap: "143000",
    clk: "1"
  });
  const response = await fetch(`${BASE}/esp32/poll?${query}`);
  return response.json();
}

let browser;
try {
  for (let i = 0; i < 60; i++) {
    try {
      await fetch(`${BASE}/health`);
      break;
    } catch {
      await new Promise((r) => setTimeout(r, 100));
    }
  }
  // Some agent activity and a board, so every card has something to show.
  const now = Date.now();
  for (const event of [
    { source: "mac-codex", task: "mac-codex-runtime", status: "running", time: new Date(now - 90_000).toISOString() },
    { source: "hermes", task: "pet review", sessionId: "h1", status: "completed", time: new Date(now - 40_000).toISOString() },
    { source: "mac-claude", task: "mac-claude-session", status: "tool-use", time: new Date(now - 5_000).toISOString() }
  ]) {
    await api("/events", { method: "POST", body: JSON.stringify({ ...event, notify: false }) });
  }
  await boardPoll();
  const boardTimer = setInterval(() => void boardPoll().catch(() => {}), 1000);

  browser = await chromium.launch({ args: ["--autoplay-policy=no-user-gesture-required"] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const consoleErrors = [];
  page.on("console", (message) => {
    if (message.type() === "error") consoleErrors.push(message.text());
  });
  page.on("pageerror", (error) => consoleErrors.push(String(error)));

  console.log("dashboard");
  await page.goto(`${BASE}/ui/#token=${TOKEN}`);
  await page.waitForSelector("#boardKv dd");
  check(!page.url().includes("token="), "token is removed from the address bar");
  check((await page.textContent("#bridgePill")).includes("令牌保护"), "bridge pill shows token protection");
  check((await page.textContent("#boardPill")).includes("在线"), "board pill shows the fake board online");
  check((await page.locator(".face").count()) === 7, "seven clock faces");
  check((await page.locator(".agent").count()) >= 3, "agents listed");
  check((await page.textContent("#nowSource")).includes("Claude"), "latest active agent is shown");
  if (shots) await page.screenshot({ path: join(shots, "dashboard-desktop.png"), fullPage: true });

  console.log("clock face");
  await page.click('.face[data-style="words"]');
  await page.waitForFunction(() => document.querySelector('.face[data-style="words"]').getAttribute("aria-pressed") === "true");
  const afterFace = await boardPoll();
  check(afterFace.display?.clock_style === "words", "board receives the new face");
  check(afterFace.display?.page === "clock", "board is sent to the clock page");
  await page.click("#hour12Toggle + .track");
  await page.waitForTimeout(400);
  check((await boardPoll()).display?.hour12 === true, "12-hour toggle reaches the board");

  console.log("converter");
  await page.setInputFiles("#fileInput", video);
  await page.waitForSelector("#studio:not([hidden])");
  await page.waitForTimeout(600);
  const estimate = await page.textContent("#estimate");
  check(/帧/.test(estimate), `estimate shown (${estimate.trim()})`);
  const inkRatio = await page.evaluate(() => {
    const canvas = document.querySelector("#bitCanvas");
    const { data } = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height);
    let ink = 0;
    for (let i = 0; i < data.length; i += 4) if (data[i] < 100) ink++;
    return ink / (data.length / 4);
  });
  check(inkRatio > 0.03 && inkRatio < 0.6, `1-bit preview has ink (${(inkRatio * 100).toFixed(1)}%)`);
  await page.fill("#animName", "e2e-clip");
  if (shots) await page.screenshot({ path: join(shots, "studio.png"), fullPage: false, clip: await page.locator("#studioCard").boundingBox() });
  await page.click("#convertBtn");
  await page.waitForFunction(() => document.querySelector("#convertBtn").hidden === false && document.querySelector("#progress").hidden, null, { timeout: 60_000 });
  const toastText = await page.textContent("#toast");
  check(toastText.includes("已上传 e2e-clip"), `upload toast (${toastText.trim()})`);
  const library = await api("/anim");
  const clip = library.animations.find((item) => item.id === "e2e-clip");
  check(Boolean(clip), "animation stored on the bridge");
  if (clip) {
    check(clip.frames === 80, `80 frames for 4 s at 20 fps (got ${clip.frames})`);
    check(clip.width === 400 && clip.height === 300, `fitted to 400x300 (got ${clip.width}x${clip.height})`);
  }
  await page.waitForSelector(".anim");

  console.log("playback");
  await page.check("#soundToggle");
  await page.evaluate(() => {
    // Seeking a playing video drops its sound; the sync must only seek before the start.
    window.__seeks = [];
    document.querySelector("#srcVideo").addEventListener("seeking", () => window.__seeks.push(Date.now()));
  });
  const eggReply = page.waitForResponse((response) => response.url().endsWith("/egg/play"));
  await page.click('.anim >> text="▶ 板子播放"');
  const startAtMs = (await (await eggReply).json()).egg?.startAtMs || 0;
  await page.waitForTimeout(300);
  const polled = await boardPoll();
  check(polled.egg?.id === "e2e-clip", "board receives the egg command");
  check(polled.egg?.start_at_ms > polled.server_time_ms, "egg starts in the future");
  await page.waitForTimeout(Math.max(0, startAtMs + 700 - Date.now()));
  const playing = await page.evaluate(() => ({
    label: document.querySelector("#mirrorLabel").textContent,
    videoPlaying: !document.querySelector("#srcVideo").paused,
    muted: document.querySelector("#srcVideo").muted
  }));
  check(playing.label.includes("板子播放中"), `mirror follows the board (${playing.label})`);
  check(playing.videoPlaying && !playing.muted, "local video plays with sound in sync");
  if (shots) await page.screenshot({ path: join(shots, "library-playing.png"), clip: await page.locator("#libraryCard").boundingBox() });
  // The clip is 4 s long: by start + 4.6 s the sound has stopped on its own.
  await page.waitForTimeout(Math.max(0, startAtMs + 4_600 - Date.now()));
  const afterClip = await page.evaluate((start) => {
    const video = document.querySelector("#srcVideo");
    return { seeks: window.__seeks.filter((at) => at > start + 300).length, paused: video.paused, muted: video.muted };
  }, startAtMs);
  check(afterClip.seeks === 0, `sound plays through without seeking (${afterClip.seeks} seeks after the start)`);
  check(afterClip.paused && afterClip.muted, "sound stops at the end of the clip");
  await page.click("#stopEgg");
  await page.waitForTimeout(400);
  check((await boardPoll()).egg === undefined, "stop clears the egg command");

  console.log("default animation");
  await page.click('.anim >> text="设为默认"');
  await page.waitForTimeout(300);
  check((await api("/settings")).settings.defaultAnimation === "e2e-clip", "default animation saved");

  console.log("mobile layout");
  await page.setViewportSize({ width: 390, height: 844 });
  await page.waitForTimeout(300);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  check(overflow <= 0, `no horizontal scroll on a phone (overflow ${overflow}px)`);
  if (shots) await page.screenshot({ path: join(shots, "dashboard-mobile.png"), fullPage: true });

  console.log("dark mode");
  await page.emulateMedia({ colorScheme: "dark" });
  await page.setViewportSize({ width: 1280, height: 900 });
  await page.waitForTimeout(500);
  const darkButton = await page.evaluate(() => getComputedStyle(document.querySelector(".anim .ghost")).backgroundColor);
  check(darkButton === "rgb(34, 38, 42)", `dark theme applies to buttons (${darkButton})`);
  if (shots) await page.screenshot({ path: join(shots, "dashboard-dark.png"), fullPage: true });

  check(consoleErrors.length === 0, `no console errors${consoleErrors.length ? `: ${consoleErrors.join(" | ")}` : ""}`);
  clearInterval(boardTimer);
} catch (error) {
  failures.push(String(error?.stack || error));
  console.error(error);
} finally {
  await browser?.close();
  server.kill();
  await rm(root, { recursive: true, force: true });
}

console.log(failures.length ? `\n${failures.length} check(s) failed` : "\nall checks passed");
process.exit(failures.length ? 1 : 0);
