import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import http from "node:http";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { parseRla, recordsChunk } from "../src/rla-codec.js";
import { boxAnimation } from "../tools/gen-rla-vectors.mjs";

const TOKEN = "test-token-0123456789";
const PORT = 17400 + (process.pid % 400);
const BASE = `http://127.0.0.1:${PORT}`;

let server;
let tempRoot;

test.before(async () => {
  tempRoot = await mkdtemp(join(tmpdir(), "pet-board-api-"));
  await writeFile(join(tempRoot, "token"), `${TOKEN}\n`, { mode: 0o600 });
  server = spawn(process.execPath, ["./src/bridge-server.js"], {
    cwd: process.cwd(),
    env: {
      ...process.env,
      PET_BRIDGE_TOKEN: "",
      PET_BRIDGE_TOKEN_FILE: join(tempRoot, "token"),
      PET_BRIDGE_PORT: String(PORT),
      PET_BRIDGE_LOG: join(tempRoot, "events.jsonl"),
      PET_BRIDGE_STATE: join(tempRoot, "bridge-state.json"),
      PET_BRIDGE_CLAUDE_PROJECTS: join(tempRoot, "claude-projects"),
      PET_BRIDGE_CODEX_STATE: join(tempRoot, "missing.sqlite"),
      PET_BRIDGE_EGG_DELAY_MS: "2000"
    },
    stdio: ["ignore", "pipe", "pipe"]
  });
  let output = "";
  server.stdout.on("data", (chunk) => (output += chunk));
  server.stderr.on("data", (chunk) => (output += chunk));
  for (let i = 0; i < 80; i++) {
    try {
      const response = await fetch(`${BASE}/health`);
      if (response.status) return;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(`bridge did not start:\n${output}`);
});

test.after(async () => {
  server?.kill();
  await rm(tempRoot, { recursive: true, force: true });
});

function api(path, { method = "GET", body, headers = {}, token = TOKEN } = {}) {
  const init = { method, headers: { ...headers } };
  if (token) init.headers.authorization = `Bearer ${token}`;
  if (body instanceof Uint8Array) {
    init.body = body;
    init.headers["content-type"] = "application/octet-stream";
  } else if (body !== undefined) {
    init.body = JSON.stringify(body);
    init.headers["content-type"] = "application/json";
  }
  return fetch(`${BASE}${path}`, init);
}

async function poll(query = "") {
  const response = await fetch(`${BASE}/esp32/poll?token=${TOKEN}${query}`);
  assert.equal(response.status, 200);
  return response.json();
}

test("the token file protects the API; the dashboard shell is public", async () => {
  assert.equal((await api("/status", { token: "" })).status, 401);
  assert.equal((await api("/status", { token: "wrong" })).status, 401);
  const ui = await fetch(`${BASE}/ui/`);
  assert.equal(ui.status, 200);
  assert.match(ui.headers.get("content-type"), /text\/html/);
  const codec = await fetch(`${BASE}/ui/rla-codec.js`);
  assert.equal(codec.status, 200);
  assert.match(await codec.text(), /export function packBits/);
  assert.equal((await fetch(`${BASE}/ui/..%2Fpackage.json`)).status, 404);
  assert.equal((await fetch(`${BASE}/`, { redirect: "manual" })).headers.get("location"), "/ui/");
});

test("private API rejects other websites and rebinding host names", async () => {
  const foreign = await api("/status", { headers: { origin: "https://example.com" } });
  assert.equal(foreign.status, 403);
  // fetch() will not let us fake the Host header, a DNS-rebinding page can.
  const rebindingStatus = await new Promise((resolve, reject) => {
    const request = http.request(
      { host: "127.0.0.1", port: PORT, path: "/status", headers: { host: `evil.example:${PORT}`, authorization: `Bearer ${TOKEN}` } },
      (response) => {
        response.resume();
        resolve(response.statusCode);
      }
    );
    request.on("error", reject);
    request.end();
  });
  assert.equal(rebindingStatus, 403);
  const own = await api("/status", { headers: { origin: BASE } });
  assert.equal(own.status, 200);
  assert.equal(own.headers.get("access-control-allow-origin"), null);
});

test("poll carries server time, timezone and records board telemetry", async () => {
  const before = Date.now();
  const body = await poll("&fw=1.3.0&bat=77&temp=21.5&hum=40&rssi=-50&page=clock&style=sans&eggreq=0&clk=1");
  assert.equal(body.ok, true);
  assert.ok(body.server_time_ms >= before && body.server_time_ms <= Date.now());
  assert.equal(typeof body.time?.tz, "string");
  assert.ok(body.time.tz.length > 1);
  assert.equal(body.egg, undefined);
  const status = await (await api("/status")).json();
  assert.equal(status.device.online, true);
  assert.equal(status.device.firmware, "1.3.0");
  assert.equal(status.device.battery, 77);
  assert.equal(status.device.page, "clock");
  assert.equal(status.bridge.tokenRequired, true);
});

test("settings reach the board as a revisioned display command", async () => {
  const bad = await api("/settings", { method: "POST", body: { clockStyle: "comic" } });
  assert.equal(bad.status, 400);
  const response = await api("/settings", {
    method: "POST",
    body: { clockStyle: "analog", hour12: true, page: "clock" }
  });
  assert.equal(response.status, 200);
  const first = await poll();
  assert.equal(first.display.clock_style, "analog");
  assert.equal(first.display.hour12, true);
  assert.equal(first.display.page, "clock");
  await api("/settings", { method: "POST", body: { showSeconds: false } });
  const second = await poll();
  assert.ok(second.display.rev > first.display.rev);
  assert.equal(second.display.show_seconds, false);
  assert.equal(second.display.page, undefined, "page switch is one-shot");
  await api("/settings", { method: "POST", body: { tzOverride: "GMT0BST,M3.5.0/1,M10.5.0" } });
  assert.equal((await poll()).time.tz, "GMT0BST,M3.5.0/1,M10.5.0");
});

test("animations upload, stream in chunks and play on request", async () => {
  const { bytes } = boxAnimation();
  assert.equal((await api("/anim/Bad%20Name", { method: "PUT", body: bytes })).status, 400);
  assert.equal((await api("/anim/broken", { method: "PUT", body: Uint8Array.of(1, 2, 3) })).status, 400);
  const put = await api("/anim/demo", { method: "PUT", body: bytes });
  assert.equal(put.status, 201);
  assert.equal((await put.json()).animation.frames, 7);
  const list = await (await api("/anim")).json();
  assert.deepEqual(list.animations.map((item) => item.id), ["demo"]);

  const chunk = await fetch(`${BASE}/esp32/anim/demo/frames?token=${TOKEN}&start=2&count=3&max_bytes=16384`);
  assert.equal(chunk.status, 200);
  assert.equal(chunk.headers.get("x-rla-start"), "2");
  assert.equal(chunk.headers.get("x-rla-count"), "3");
  const expected = recordsChunk(bytes, parseRla(bytes), 2, 3, 16384);
  assert.deepEqual(new Uint8Array(await chunk.arrayBuffer()), expected.bytes);
  assert.equal((await fetch(`${BASE}/esp32/anim/demo/frames?start=0&count=1`)).status, 401);
  assert.equal((await fetch(`${BASE}/esp32/anim/nope/frames?token=${TOKEN}&start=0&count=1`)).status, 404);

  const play = await api("/egg/play", { method: "POST", body: { id: "demo" } });
  assert.equal(play.status, 200);
  const polled = await poll();
  assert.equal(polled.egg.id, "demo");
  assert.equal(polled.egg.frames, 7);
  assert.equal(polled.egg.width, 64);
  assert.ok(polled.egg.start_at_ms > polled.server_time_ms);
  await api("/egg/stop", { method: "POST", body: {} });
  assert.equal((await poll()).egg, undefined);
});

test("the board's secret gesture plays the default animation", async () => {
  await api("/settings", { method: "POST", body: { defaultAnimation: "demo" } });
  await poll("&eggreq=5");  // counts as a press after the earlier 0; start clean
  await api("/egg/stop", { method: "POST", body: {} });
  assert.equal((await poll("&eggreq=5")).egg, undefined, "the same counter does not replay");
  assert.equal((await poll()).egg, undefined, "a poll without the counter changes nothing");
  const body = await poll("&eggreq=6");
  assert.equal(body.egg?.id, "demo");
  const status = await (await api("/status")).json();
  assert.equal(status.egg.playing, true);
  await api("/egg/stop", { method: "POST", body: {} });
  assert.equal((await api("/anim/demo", { method: "DELETE" })).status, 200);
  assert.deepEqual((await (await api("/anim")).json()).animations, []);
});
