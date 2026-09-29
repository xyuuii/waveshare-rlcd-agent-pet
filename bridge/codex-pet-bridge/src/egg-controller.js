// Keeps the "play this animation at this time" instruction that rides on
// /esp32/poll until the animation has had time to finish.

export class EggController {
  constructor({ now = () => Date.now() } = {}) {
    this.now = now;
    this.current = null;
    this.lastRev = 0;
  }

  play(meta, { delayMs = 4000 } = {}) {
    const now = this.now();
    // Seconds since epoch: monotonic across bridge restarts, fits the board's u32.
    const rev = Math.max(this.lastRev + 1, Math.floor(now / 1000));
    this.lastRev = rev;
    const startAtMs = now + Math.max(1000, Math.min(15_000, delayMs));
    const durationMs = Math.ceil((meta.frames * 100_000) / meta.fpsX100);
    this.current = {
      rev,
      id: meta.id,
      frames: meta.frames,
      fpsX100: meta.fpsX100,
      width: meta.width,
      height: meta.height,
      startAtMs,
      endsAtMs: startAtMs + durationMs,
      requestedAt: now
    };
    return this.state();
  }

  stop() {
    this.current = null;
    return this.state();
  }

  active(now = this.now()) {
    if (this.current && now > this.current.endsAtMs + 5000) this.current = null;
    return this.current;
  }

  // The object the board receives, or null.
  command(now = this.now()) {
    const egg = this.active(now);
    if (!egg) return null;
    return {
      rev: egg.rev,
      id: egg.id,
      frames: egg.frames,
      fps_x100: egg.fpsX100,
      width: egg.width,
      height: egg.height,
      start_at_ms: egg.startAtMs
    };
  }

  state(now = this.now()) {
    const egg = this.active(now);
    if (!egg) return { playing: false };
    return {
      playing: true,
      ...egg,
      phase: now < egg.startAtMs ? "countdown" : "playing",
      positionMs: Math.max(0, now - egg.startAtMs)
    };
  }
}
