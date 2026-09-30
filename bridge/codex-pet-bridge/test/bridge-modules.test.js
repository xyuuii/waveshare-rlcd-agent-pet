import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { mkdtemp, rm, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { AnimStore } from "../src/anim-store.js";
import { DeviceRegistry, parseTelemetry } from "../src/device-registry.js";
import { SettingsStore, applySettingsPatch, defaultSettings, displayCommand } from "../src/display-settings.js";
import { EggController } from "../src/egg-controller.js";
import { posixTzFromTzif, readSystemTimezone, zoneNameFromLocaltimeLink } from "../src/system-timezone.js";
import { resolveBridgeToken } from "../src/token-file.js";
import { boxAnimation } from "../tools/gen-rla-vectors.mjs";

const NOW = Date.parse("2026-09-29T09:00:00Z");

test("settings patches validate input and move the revision only for display changes", () => {
  const first = applySettingsPatch(defaultSettings(), { clockStyle: "segment" }, { now: NOW });
  assert.deepEqual(first.errors, []);
  assert.equal(first.settings.clockStyle, "segment");
  assert.equal(first.settings.rev, Math.floor(NOW / 1000));
  const same = applySettingsPatch(first.settings, { clockStyle: "segment" }, { now: NOW + 5000 });
  assert.equal(same.changed, false);
  assert.equal(same.settings.rev, first.settings.rev);
  const tzOnly = applySettingsPatch(first.settings, { tzOverride: "UTC0" }, { now: NOW + 5000 });
  assert.equal(tzOnly.settings.rev, first.settings.rev, "tz is sent separately and does not bump rev");
  const bad = applySettingsPatch(first.settings, { clockStyle: "comic", hour12: "yes", page: "secret", tzOverride: "x y" });
  assert.equal(bad.errors.length, 4);
});

test("page is a one-shot instruction tied to its revision", () => {
  const withPage = applySettingsPatch(defaultSettings(), { page: "clock" }, { now: NOW }).settings;
  assert.equal(displayCommand(withPage).page, "clock");
  const later = applySettingsPatch(withPage, { hour12: true }, { now: NOW + 1000 }).settings;
  assert.equal(displayCommand(later).page, undefined);
  assert.equal(displayCommand(later).hour12, true);
  assert.equal(displayCommand(defaultSettings()), null);
});

test("settings store persists atomically", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pet-settings-"));
  try {
    const store = new SettingsStore(join(dir, "settings.json"));
    await store.load();
    const result = await store.update({ clockStyle: "words", defaultAnimation: "badapple" }, { now: NOW });
    assert.deepEqual(result.errors, []);
    const again = new SettingsStore(join(dir, "settings.json"));
    assert.equal((await again.load()).clockStyle, "words");
    assert.equal(again.get().defaultAnimation, "badapple");
    const rejected = await again.update({ clockStyle: "nope" });
    assert.equal(rejected.errors.length, 1);
    assert.equal(again.get().clockStyle, "words");
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("telemetry parsing and the secret-gesture counter", () => {
  const params = new URLSearchParams("focus=auto&fw=1.3.0&bat=84&mv=3980&temp=22.6&hum=48&rssi=-52&up=60&page=clock&style=words&srev=7&erev=3&heap=123456&clk=1&eggreq=2");
  const telemetry = parseTelemetry(params);
  assert.equal(telemetry.firmware, "1.3.0");
  assert.equal(telemetry.battery, 84);
  assert.equal(telemetry.temperatureC, 22.6);
  assert.equal(telemetry.page, "clock");
  assert.equal(telemetry.timeValid, true);
  assert.equal(telemetry.eggRequest, 2);
  assert.equal(parseTelemetry(new URLSearchParams("")).battery, null);
  assert.equal(parseTelemetry(new URLSearchParams("")).eggRequest, null);

  const registry = new DeviceRegistry({ onlineWindowMs: 10_000 });
  assert.deepEqual(registry.snapshot(NOW), { seen: false, online: false });
  const board = { address: "192.168.1.50" };
  const press = (eggRequest, at) => registry.update({ ...telemetry, eggRequest }, { ...board, now: NOW + at }).eggRequested;
  // First sighting after a bridge restart never replays an old request.
  assert.equal(press(2, 0), false);
  assert.equal(press(2, 500), false, "same counter again");
  assert.equal(press(3, 1000), true);
  assert.equal(press(null, 1500), false, "a poll without the counter keeps the last value");
  assert.equal(press(3, 1800), false);
  assert.equal(press(0, 2000), false, "board rebooted");
  assert.equal(press(1, 2500), true, "first gesture after the reboot");
  const snapshot = registry.snapshot(NOW + 5000);
  assert.equal(snapshot.online, true);
  assert.equal(snapshot.address, "192.168.1.50");
  assert.equal(snapshot.polls, 7);
  assert.equal(registry.snapshot(NOW + 20_000).online, false);
});

test("egg controller schedules, expires and keeps revisions monotonic", () => {
  let clock = NOW;
  const egg = new EggController({ now: () => clock });
  assert.equal(egg.command(), null);
  const meta = { id: "demo", frames: 60, fpsX100: 2000, width: 400, height: 300 };
  const first = egg.play(meta, { delayMs: 3000 });
  assert.equal(first.phase, "countdown");
  assert.equal(egg.command().start_at_ms, NOW + 3000);
  assert.equal(egg.command().fps_x100, 2000);
  const second = egg.play(meta, { delayMs: 3000 });
  assert.ok(second.rev > first.rev);
  clock = NOW + 3000 + 3000 + 6000;  // start + 3 s of video + grace
  assert.equal(egg.command(), null);
  assert.equal(egg.state().playing, false);
  egg.play(meta);
  assert.equal(egg.stop().playing, false);
});

test("animation store validates uploads and serves chunks", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pet-anim-"));
  try {
    const store = new AnimStore(dir, { maxBytes: 4096 });
    const { bytes } = boxAnimation();
    const saved = await store.put("demo", bytes);
    assert.equal(saved.frames, 7);
    assert.equal(saved.width, 64);
    assert.deepEqual((await store.list()).map((item) => item.id), ["demo"]);
    const chunk = await store.chunk("demo", 1, 3, 1 << 20);
    assert.equal(chunk.count, 3);
    await assert.rejects(store.put("../escape", bytes), /invalid animation id/);
    await assert.rejects(store.put("broken", Uint8Array.of(1, 2, 3)), /invalid RLA1/);
    await assert.rejects(store.put("huge", new Uint8Array(5000)), /too large/);
    await assert.rejects(store.chunk("missing", 0, 1, 100), /not found/);
    await store.remove("demo");
    assert.deepEqual(await store.list(), []);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("POSIX TZ comes from the TZif footer of /etc/localtime", async () => {
  if (existsSync("/usr/share/zoneinfo/Europe/London")) {
    assert.equal(posixTzFromTzif(readFileSync("/usr/share/zoneinfo/Europe/London")), "GMT0BST,M3.5.0/1,M10.5.0");
  }
  const fake = Buffer.concat([Buffer.from("TZif2"), Buffer.alloc(60), Buffer.from("\nCST-8\n")]);
  assert.equal(posixTzFromTzif(fake), "CST-8");
  assert.equal(posixTzFromTzif(Buffer.concat([Buffer.from("TZif\0"), Buffer.alloc(60), Buffer.from("\nCST-8\n")])), "");
  assert.equal(posixTzFromTzif(Buffer.from("not a tz file at all, definitely not, no no no no\n")), "");
  assert.equal(zoneNameFromLocaltimeLink("/var/db/timezone/zoneinfo/Europe/London"), "Europe/London");
  const dir = await mkdtemp(join(tmpdir(), "pet-tz-"));
  try {
    const zoneFile = join(dir, "zoneinfo", "Asia", "Shanghai");
    await writeFile(join(dir, "placeholder"), "");
    await import("node:fs/promises").then(({ mkdir }) => mkdir(join(dir, "zoneinfo", "Asia"), { recursive: true }));
    await writeFile(zoneFile, fake);
    await symlink(zoneFile, join(dir, "localtime"));
    assert.deepEqual(readSystemTimezone({ localtimePath: join(dir, "localtime"), maxAgeMs: 0 }), {
      zone: "Asia/Shanghai",
      posix: "CST-8"
    });
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("token resolution prefers env, then files; the server never reads the default file", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pet-token-"));
  try {
    const file = join(dir, "token");
    await writeFile(file, "abc123\n");
    assert.equal(resolveBridgeToken({ env: { PET_BRIDGE_TOKEN: " direct " } }), "direct");
    assert.equal(resolveBridgeToken({ env: { PET_BRIDGE_TOKEN_FILE: file } }), "abc123");
    assert.equal(resolveBridgeToken({ env: { PET_BRIDGE_TOKEN_FILE: file }, allowDefaultFile: false }), "abc123");
    assert.equal(resolveBridgeToken({ env: {}, allowDefaultFile: false }), "");
    assert.equal(resolveBridgeToken({ env: { PET_BRIDGE_TOKEN_FILE: join(dir, "missing") } }), "");
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
