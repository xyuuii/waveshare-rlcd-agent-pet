// Keeps the dashboard in step with the board: the server clock, and the sound
// of the local video while the board plays the matching animation.
//
// Why not just seek when the sound drifts: seeking a playing video goes quiet
// until the decoder has worked its way from the previous keyframe to the new
// spot (0.1–1 s), and playback then carries on that far behind the clock. The
// first version corrected drift by seeking every 1.5 s, so on most videos it
// fell behind, seeked, fell behind again... and the sound stuttered.
//
// SoundSync seeks only while paused and ahead of time ("cueing"), starts
// playback when the clock reaches that spot, and steers small drift away by
// playing up to a few percent faster or slower; browsers keep the pitch.

export const SOUND_SYNC_DEFAULTS = Object.freeze({
  intervalMs: 200, // how often drift is measured
  window: 3, // median of this many measurements
  settleMs: 500, // measurements right after a start are noisy: skip them
  engageS: 0.03, // drift below this is left alone...
  releaseS: 0.008, // ...and steering stops once back below this
  steerS: 2, // close the gap in about this long
  maxSpeedDelta: 0.04, // never more than ±4 % speed
  recueS: 0.35, // farther off than this: cue again (one short pause)
  leadMs: 300, // cue at least this far ahead of the clock
  maxLeadMs: 4000,
  maxStartLatencyMs: 250,
  retries: 3
});

const HAVE_FUTURE_DATA = 3;

function clamp(value, low, high) {
  return Math.min(high, Math.max(low, value));
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = sorted.length >> 1;
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

export class SoundSync {
  /**
   * @param {HTMLMediaElement} media
   * @param {object} [env]  now(), setTimeout/clearTimeout/setInterval/clearInterval
   *                        (for tests) and options (see SOUND_SYNC_DEFAULTS)
   */
  constructor(media, env = {}) {
    this.media = media;
    this.now = env.now || (() => Date.now());
    this.timers = {
      setTimeout: env.setTimeout || ((fn, ms) => setTimeout(fn, ms)),
      clearTimeout: env.clearTimeout || ((id) => clearTimeout(id)),
      setInterval: env.setInterval || ((fn, ms) => setInterval(fn, ms)),
      clearInterval: env.clearInterval || ((id) => clearInterval(id))
    };
    this.options = { ...SOUND_SYNC_DEFAULTS, ...(env.options || {}) };
    this.run = null;
    // Learned as it goes: how long play() takes to be heard (later starts call
    // play() that much early) and how long this video takes to seek (later
    // cues look that much further ahead).
    this.startLatencyMs = 0;
    this.seekMs = 0;
    this.stats = { cues: 0, speedChanges: 0 };
  }

  get active() {
    return this.run !== null;
  }

  /**
   * position(nowMs) -> where in the media (seconds) the sound should be at
   * local time nowMs. It is read on every check, so a changed offset or a
   * better clock estimate takes effect while playing.
   * from, to: the clip in media seconds; the sound stops at `to`.
   * onEnd(reason): "end" at the clip end, "paused" if something else paused
   * the media (media keys, headphones), "error" if it could not seek.
   * onBlocked(error): the browser refused to play sound.
   */
  start({ position, from = 0, to = Infinity, onEnd, onBlocked }) {
    this.stop();
    const run = {
      position,
      from,
      to,
      onEnd,
      onBlocked,
      samples: [],
      steering: false,
      cueing: false,
      learn: false,
      settleUntil: 0,
      lead: 0,
      tries: 0,
      timer: 0,
      ticker: 0
    };
    this.run = run;
    const { media } = this;
    media.muted = false;
    if ("preservesPitch" in media) media.preservesPitch = true;
    else if ("webkitPreservesPitch" in media) media.webkitPreservesPitch = true;
    this.speed(1);
    run.ticker = this.timers.setInterval(() => this.tick(run), this.options.intervalMs);
    this.cue(run);
  }

  // Leaves the media alone when nothing is playing (the converter may be using it).
  stop() {
    const run = this.run;
    if (!run) return;
    this.run = null;
    this.timers.clearInterval(run.ticker);
    this.timers.clearTimeout(run.timer);
    const { media } = this;
    if (!media.paused) media.pause();
    this.speed(1);
    media.muted = true;
  }

  finish(run, reason) {
    if (this.run !== run) return;
    this.stop();
    run.onEnd?.(reason);
  }

  speed(rate) {
    const { media } = this;
    if (Math.abs(media.playbackRate - rate) >= 0.001) {
      media.playbackRate = rate;
      this.stats.speedChanges += 1;
    }
  }

  // Pause, seek to where the clock will be a little later, start right then.
  async cue(run) {
    const { media, options } = this;
    run.cueing = true;
    run.samples = [];
    run.steering = false;
    this.timers.clearTimeout(run.timer);
    this.speed(1);
    if (!media.paused) media.pause();
    const began = this.now();
    const lead = Math.min(options.maxLeadMs, Math.max(options.leadMs, this.seekMs * 1.5 + 100, run.lead));
    let target = Math.max(run.from, run.position(began + lead));
    if (Number.isFinite(media.duration)) target = Math.min(target, media.duration);
    if (target >= run.to || (Number.isFinite(media.duration) && target >= media.duration)) {
      this.finish(run, "end");
      return;
    }
    this.stats.cues += 1;
    try {
      await this.seek(target);
    } catch {
      this.finish(run, "error");
      return;
    }
    if (this.run !== run) return;
    const took = this.now() - began;
    this.seekMs = Math.max(took, this.seekMs * 0.7);
    const startIn = (target - run.position(this.now())) * 1000 - this.startLatencyMs;
    if (startIn < -30 && run.tries < options.retries) {
      // The seek outlasted the lead: cue again, further ahead.
      run.tries += 1;
      run.lead = Math.min(options.maxLeadMs, lead * 2);
      this.cue(run);
      return;
    }
    run.timer = this.timers.setTimeout(() => this.begin(run), Math.max(0, startIn));
  }

  begin(run) {
    if (this.run !== run) return;
    run.cueing = false;
    run.tries = 0;
    run.learn = true;
    run.settleUntil = this.now() + this.options.settleMs;
    let attempt;
    try {
      attempt = this.media.play();
    } catch (error) {
      attempt = Promise.reject(error);
    }
    attempt?.catch?.((error) => {
      if (this.run !== run || error?.name === "AbortError") return;
      this.stop();
      run.onBlocked?.(error);
    });
  }

  seek(time) {
    const { media } = this;
    if (!media.seeking && media.readyState >= 2 && Math.abs(media.currentTime - time) < 0.0005) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      const cleanup = () => {
        media.removeEventListener("seeked", onSeeked);
        media.removeEventListener("error", onError);
      };
      const onSeeked = () => {
        cleanup();
        resolve();
      };
      const onError = () => {
        cleanup();
        reject(new Error("seek failed"));
      };
      media.addEventListener("seeked", onSeeked);
      media.addEventListener("error", onError);
      media.currentTime = time;
    });
  }

  tick(run) {
    if (this.run !== run) return;
    const { media, options } = this;
    const now = this.now();
    if (run.position(now) >= run.to || media.ended) {
      this.finish(run, "end");
      return;
    }
    if (run.cueing) return;
    if (media.paused) {
      // Not paused by us: someone pressed pause (media keys, headphones out).
      if (now >= run.settleUntil) this.finish(run, "paused");
      return;
    }
    if (media.seeking || media.readyState < HAVE_FUTURE_DATA || now < run.settleUntil) return;

    run.samples.push(media.currentTime - run.position(now)); // > 0: sound is ahead
    if (run.samples.length > options.window) run.samples.shift();
    if (run.samples.length < options.window) return;
    const drift = median(run.samples);

    if (run.learn) {
      run.learn = false;
      if (Math.abs(drift) < options.recueS) {
        this.startLatencyMs = clamp(this.startLatencyMs - drift * 1000, 0, options.maxStartLatencyMs);
      }
    }
    if (Math.abs(drift) > options.recueS) {
      this.cue(run);
      return;
    }
    if (!run.steering && Math.abs(drift) < options.engageS) return;
    if (run.steering && Math.abs(drift) < options.releaseS) {
      run.steering = false;
      this.speed(1);
      return;
    }
    run.steering = true;
    const rate = 1 - clamp(drift / options.steerS, -options.maxSpeedDelta, options.maxSpeedDelta);
    this.speed(Math.round(rate * 400) / 400);
  }
}

// Server time minus local time, from API replies. The server read its clock
// after the request left and before the reply came back, so every reply
// bounds the difference; intersecting the bounds of recent replies gives an
// estimate that only tightens instead of jumping with each slow reply. When
// zero fits (the dashboard normally runs on the Mac that runs the bridge),
// the clocks are taken to agree exactly.
export class ServerClock {
  constructor({ window = 30 } = {}) {
    this.window = window;
    this.samples = [];
    this.offsetMs = 0;
  }

  note(serverTime, sent, received) {
    if (!Number.isFinite(serverTime) || !Number.isFinite(sent) || !(received >= sent)) return this.offsetMs;
    // ±1 ms: both clocks are read in whole milliseconds.
    const sample = { lo: serverTime - received - 1, hi: serverTime - sent + 1 };
    this.samples.push(sample);
    if (this.samples.length > this.window) this.samples.shift();
    let lo = -Infinity;
    let hi = Infinity;
    for (const item of this.samples) {
      lo = Math.max(lo, item.lo);
      hi = Math.min(hi, item.hi);
    }
    if (lo > hi) {
      // A clock was set in between: start over from this reply.
      this.samples = [sample];
      ({ lo, hi } = sample);
    }
    this.offsetMs = lo <= 0 && hi >= 0 ? 0 : (lo + hi) / 2;
    return this.offsetMs;
  }
}
