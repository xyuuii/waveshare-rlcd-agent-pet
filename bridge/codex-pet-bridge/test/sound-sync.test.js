import assert from "node:assert/strict";
import { test } from "node:test";

import { ServerClock, SoundSync } from "../ui/sound-sync.js";

// Virtual time: timers fire in order as the clock is advanced.
class FakeClock {
  constructor() {
    this.t = 1_700_000_000_000;
    this.queue = [];
    this.cancelled = new Set();
    this.seq = 0;
    this.now = () => this.t;
    this.setTimeout = (fn, ms = 0) => {
      const id = ++this.seq;
      this.queue.push({ id, at: this.t + Math.max(0, ms), fn });
      return id;
    };
    this.clearTimeout = (id) => {
      this.cancelled.add(id);
      this.queue = this.queue.filter((item) => item.id !== id);
    };
    this.setInterval = (fn, ms) => {
      const id = ++this.seq;
      const entry = {
        id,
        at: this.t + ms,
        fn: () => {
          fn();
          if (!this.cancelled.has(id)) {
            entry.at = this.t + ms;
            this.queue.push(entry);
          }
        }
      };
      this.queue.push(entry);
      return id;
    };
    this.clearInterval = this.clearTimeout;
  }

  async advance(ms, onStep) {
    const end = this.t + ms;
    for (;;) {
      this.queue.sort((a, b) => a.at - b.at || a.id - b.id);
      const next = this.queue[0];
      if (!next || next.at > end) break;
      this.queue.shift();
      this.t = next.at;
      next.fn();
      await new Promise((resolve) => setImmediate(resolve));
      onStep?.();
    }
    this.t = end;
  }
}

// A media element whose clock stops while seeking and while starting up,
// like a real one. clockRate != 1 models a sound card clock that runs a
// little fast or slow against the system clock.
class FakeMedia {
  constructor(clock, { seekMs = 400, startMs = 60, duration = 300, clockRate = 1, blockPlay = false } = {}) {
    Object.assign(this, { clock, seekMs, startMs, duration, clockRate, blockPlay });
    this.listeners = new Map();
    this.paused = true;
    this.seeking = false;
    this.muted = true;
    this.ended = false;
    this.readyState = 4;
    this.preservesPitch = true;
    this.rate = 1;
    this.base = 0;
    this.anchor = clock.now();
    this.running = false;
    this.seeks = [];
    this.plays = 0;
    this.rates = [];
  }

  mediaTime() {
    if (!this.running) return this.base;
    return this.base + ((this.clock.now() - this.anchor) / 1000) * this.rate * this.clockRate;
  }

  rebase() {
    this.base = this.mediaTime();
    this.anchor = this.clock.now();
  }

  get currentTime() {
    return this.seeking ? this.target : Math.min(this.duration, this.mediaTime());
  }

  set currentTime(value) {
    this.rebase();
    this.running = false;
    this.seeking = true;
    this.target = value;
    this.seeks.push({ at: this.clock.now(), to: value, paused: this.paused });
    this.clock.clearTimeout(this.seekTimer);
    this.clock.clearTimeout(this.startTimer);
    this.seekTimer = this.clock.setTimeout(() => {
      this.seeking = false;
      this.base = value;
      this.anchor = this.clock.now();
      if (!this.paused) this.startSoon();
      this.emit("seeked");
    }, this.seekMs);
  }

  get playbackRate() {
    return this.rate;
  }

  set playbackRate(value) {
    this.rebase();
    this.rate = value;
    this.rates.push(value);
  }

  startSoon() {
    this.clock.clearTimeout(this.startTimer);
    this.startTimer = this.clock.setTimeout(() => {
      if (this.paused || this.seeking) return;
      this.anchor = this.clock.now();
      this.running = true;
    }, this.startMs);
  }

  play() {
    this.plays += 1;
    if (this.blockPlay) {
      const error = new Error("blocked");
      error.name = "NotAllowedError";
      return Promise.reject(error);
    }
    if (this.paused) {
      this.paused = false;
      if (!this.seeking) this.startSoon();
    }
    return Promise.resolve();
  }

  pause() {
    this.rebase();
    this.running = false;
    this.paused = true;
    this.clock.clearTimeout(this.startTimer);
  }

  addEventListener(type, fn) {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type).add(fn);
  }

  removeEventListener(type, fn) {
    this.listeners.get(type)?.delete(fn);
  }

  emit(type) {
    for (const fn of [...(this.listeners.get(type) || [])]) fn();
  }
}

// The board starts `countdownMs` from now; the clip starts at `from`.
function setup({ countdownMs = 3500, from = 7.3, length = 60, media: mediaOptions = {}, options } = {}) {
  const clock = new FakeClock();
  const media = new FakeMedia(clock, mediaOptions);
  const sync = new SoundSync(media, { ...clock, options });
  const startAt = clock.now() + countdownMs;
  const timeline = { offsetMs: 0 };
  const ended = [];
  const blocked = [];
  const position = (now) => from + (now - startAt - timeline.offsetMs) / 1000;
  sync.start({
    position,
    from,
    to: from + length,
    onEnd: (reason) => ended.push(reason),
    onBlocked: (error) => blocked.push(error)
  });
  // Records the media clock every 10 ms of virtual time.
  const trace = [];
  const record = async (ms) => {
    for (let elapsed = 0; elapsed < ms; elapsed += 10) {
      await clock.advance(10);
      trace.push({ at: clock.now(), media: media.currentTime, paused: media.paused, drift: media.currentTime - position(clock.now()) });
    }
  };
  return { clock, media, sync, startAt, timeline, ended, blocked, position, trace, record };
}

function stallsAfter(trace, from) {
  // Media clock frozen while it should be playing: what a listener hears as a gap.
  let frozen = 0;
  let longest = 0;
  for (let i = 1; i < trace.length; i++) {
    if (trace[i].at < from || trace[i].paused) continue;
    frozen = trace[i].media === trace[i - 1].media ? frozen + 10 : 0;
    longest = Math.max(longest, frozen);
  }
  return longest;
}

test("a slow seek is done before the start, then the sound runs without seeking again", async () => {
  const { media, sync, startAt, trace, record } = setup({ media: { seekMs: 400, startMs: 60 } });
  await record(20_000);
  assert.equal(media.seeks.length, 1, "only the cue before the start seeks");
  assert.ok(media.seeks[0].at < startAt && media.seeks[0].paused, "the cue happens paused, during the countdown");
  assert.equal(media.plays, 1);
  assert.ok(stallsAfter(trace, startAt + 200) === 0, "no gaps once playing");
  const last = trace.at(-1).drift;
  assert.ok(Math.abs(last) < 0.012, `in step at the end (drift ${(last * 1000).toFixed(1)} ms)`);
  assert.ok(Math.abs(sync.startLatencyMs - 60) <= 15, `learned the start latency (${sync.startLatencyMs.toFixed(0)} ms)`);
  assert.ok(media.rates.every((rate) => rate >= 0.96 && rate <= 1.04), "speed stays within ±4 %");
});

test("a later start uses the learned latency and is in step right away", async () => {
  const first = setup({ media: { seekMs: 300, startMs: 80 } });
  await first.record(8_000);
  const latency = first.sync.startLatencyMs;
  assert.ok(latency > 50, `learned ${latency} ms`);
  const startAt = first.clock.now() + 3_000;
  const position = (now) => 20 + (now - startAt) / 1000;
  first.sync.start({ position, from: 20, to: 80 });
  const drifts = [];
  for (let elapsed = 0; elapsed < 5_000; elapsed += 10) {
    await first.clock.advance(10);
    if (first.clock.now() > startAt + 300) drifts.push(first.media.currentTime - position(first.clock.now()));
  }
  const worst = Math.max(...drifts.map(Math.abs));
  assert.ok(worst < 0.02, `second start within 20 ms (worst ${(worst * 1000).toFixed(1)} ms)`);
});

test("a seek that outlasts the countdown is cued again further ahead", async () => {
  const { media, startAt, trace, record } = setup({ countdownMs: 100, media: { seekMs: 900 } });
  await record(12_000);
  assert.ok(media.seeks.length <= 3, `${media.seeks.length} cues`);
  assert.ok(media.seeks.every((seek) => seek.paused), "every seek happens paused");
  const lastSeek = media.seeks.at(-1).at;
  assert.equal(stallsAfter(trace, lastSeek + 2_000), 0, "no gaps after the start");
  const last = trace.at(-1).drift;
  assert.ok(Math.abs(last) < 0.012, `in step (drift ${(last * 1000).toFixed(1)} ms)`);
  assert.ok(startAt < lastSeek, "the board had already started; the sound joined late but in step");
});

test("a sound card clock running 0.4 % fast is steered with speed, never with seeks", async () => {
  const { media, sync, startAt, trace, record } = setup({ media: { clockRate: 1.004 }, length: 120 });
  await record(90_000);
  assert.equal(media.seeks.length, 1, "no seeks after the cue");
  const settled = trace.filter((row) => row.at > startAt + 4_000).map((row) => Math.abs(row.drift));
  const worst = Math.max(...settled);
  assert.ok(worst < 0.04, `stays within 40 ms (worst ${(worst * 1000).toFixed(1)} ms; uncorrected it would reach ~350 ms)`);
  assert.ok(sync.stats.speedChanges > 0, "speed was adjusted");
  assert.ok(media.rates.every((rate) => rate >= 0.96 && rate <= 1.04), "speed stays within ±4 %");
  assert.equal(stallsAfter(trace, startAt + 200), 0, "no gaps");
});

test("nudging the offset a little is steered, a big change cues once", async () => {
  const { media, timeline, startAt, trace, record } = setup({});
  await record(6_000);
  timeline.offsetMs = 60; // sound 60 ms later
  await record(6_000);
  assert.equal(media.seeks.length, 1, "60 ms is steered, not seeked");
  assert.ok(Math.abs(trace.at(-1).drift) < 0.012, `in step after a small change (${(trace.at(-1).drift * 1000).toFixed(1)} ms)`);
  timeline.offsetMs = -440; // sound 500 ms earlier
  await record(6_000);
  assert.equal(media.seeks.length, 2, "a big change cues exactly once");
  assert.ok(media.seeks[1].paused, "the second cue seeks while paused");
  assert.ok(Math.abs(trace.at(-1).drift) < 0.012, `in step after a big change (${(trace.at(-1).drift * 1000).toFixed(1)} ms)`);
  assert.ok(stallsAfter(trace, startAt + 200) < 100, "apart from the re-cue's pause, no gaps");
});

test("stops and mutes at the end of the clip", async () => {
  const { media, sync, ended, record } = setup({ length: 4 });
  await record(9_000);
  assert.equal(sync.active, false);
  assert.equal(media.paused, true);
  assert.equal(media.muted, true);
  assert.deepEqual(ended, ["end"]);
});

test("a pause from outside (media keys, headphones) ends the sync quietly", async () => {
  const { media, sync, ended, record } = setup({});
  await record(6_000);
  media.pause();
  await record(1_000);
  assert.equal(sync.active, false);
  assert.equal(media.muted, true);
  assert.deepEqual(ended, ["paused"]);
});

test("a refused play() is reported once and stops", async () => {
  const { sync, blocked, record } = setup({ media: { blockPlay: true } });
  await record(5_000);
  assert.equal(sync.active, false);
  assert.equal(blocked.length, 1);
  assert.equal(blocked[0].name, "NotAllowedError");
});

test("stop() does not touch the media when nothing is playing", () => {
  const clock = new FakeClock();
  const media = new FakeMedia(clock);
  media.paused = false; // e.g. the converter is playing it
  media.muted = true;
  media.rate = 3;
  const sync = new SoundSync(media, clock);
  sync.stop();
  assert.equal(media.paused, false);
  assert.equal(media.playbackRate, 3);
});

test("server clock: same machine gives exactly zero despite slow replies", () => {
  const clock = new ServerClock();
  // Replies that took 5–300 ms on the server, stamped somewhere in between.
  for (const [took, stampAt] of [[5, 0.5], [300, 0.9], [120, 0.1], [40, 0.6], [250, 0.3]]) {
    const sent = 1_000_000;
    clock.note(sent + took * stampAt, sent, sent + took);
    assert.equal(clock.offsetMs, 0);
  }
});

test("server clock: another device's clock offset is found and only tightens", () => {
  const clock = new ServerClock();
  const truth = -40; // that device runs 40 ms ahead of the Mac
  let sent = 5_000_000;
  const estimates = [];
  for (const [rtt, stampAt] of [[30, 0.8], [12, 0.5], [60, 0.2], [8, 0.4], [25, 0.7]]) {
    clock.note(sent + truth + rtt * stampAt, sent, sent + rtt);
    estimates.push(clock.offsetMs);
    sent += 2_000;
  }
  assert.ok(Math.abs(estimates.at(-1) - truth) <= 5, `estimate ${estimates.at(-1)} near ${truth}`);
  // A slow reply later on does not move a good estimate.
  clock.note(sent + truth + 400 * 0.9, sent, sent + 400);
  assert.equal(clock.offsetMs, estimates.at(-1));
});

test("server clock: starts over when a clock is set", () => {
  const clock = new ServerClock();
  clock.note(1_000_010, 1_000_000, 1_000_020);
  assert.equal(clock.offsetMs, 0);
  clock.note(2_005_000 + 10, 2_000_000, 2_000_020); // the Mac's clock jumped 5 s ahead
  assert.ok(Math.abs(clock.offsetMs - 5_000) <= 11, `${clock.offsetMs}`);
});
