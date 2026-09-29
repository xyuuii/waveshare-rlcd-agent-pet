// Turns a local video into an RLA1 1-bit animation, entirely in the browser.
// Frames are sampled at a fixed rate: target frame i shows whatever the video
// shows at start + i / fps, the same rule the board uses when it plays back.

import { RlaEncoder, grayToBitmap, rgbaToGray } from "./rla-codec.js";

export const PANEL_WIDTH = 400;
export const PANEL_HEIGHT = 300;
export const INK = [35, 38, 42];
export const PAPER = [214, 217, 207];

export const SIZE_PRESETS = {
  full: { maxWidth: 400, maxHeight: 300 },
  half: { maxWidth: 200, maxHeight: 150 }
};

export function fitSize(sourceWidth, sourceHeight, maxWidth, maxHeight) {
  if (!sourceWidth || !sourceHeight) return { width: maxWidth, height: maxHeight };
  const scale = Math.min(maxWidth / sourceWidth, maxHeight / sourceHeight);
  return {
    width: Math.max(8, Math.min(maxWidth, Math.round(sourceWidth * scale))),
    height: Math.max(8, Math.min(maxHeight, Math.round(sourceHeight * scale)))
  };
}

// Same integer scale the firmware picks (anim_codec.cpp animFitScale).
export function panelScale(width, height) {
  return Math.max(1, Math.min(Math.floor(PANEL_WIDTH / width), Math.floor(PANEL_HEIGHT / height)));
}

export class FrameProcessor {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.canvas = document.createElement("canvas");
    this.canvas.width = width;
    this.canvas.height = height;
    this.context = this.canvas.getContext("2d", { willReadFrequently: true, alpha: false });
    this.context.imageSmoothingEnabled = true;
    this.context.imageSmoothingQuality = "high";
  }

  bitmapFrom(source, options) {
    const { context, width, height } = this;
    context.drawImage(source, 0, 0, width, height);
    const { data } = context.getImageData(0, 0, width, height);
    return grayToBitmap(rgbaToGray(data, width, height), width, height, options);
  }
}

// Draws a 1-bit frame onto a 400x300 canvas the way the board would: centred,
// scaled by a whole number, ink on paper.
export class PanelPainter {
  constructor(canvas) {
    this.canvas = canvas;
    this.context = canvas.getContext("2d");
    this.scratch = document.createElement("canvas");
    this.image = null;
  }

  clear() {
    const { context, canvas } = this;
    context.fillStyle = `rgb(${PAPER.join(",")})`;
    context.fillRect(0, 0, canvas.width, canvas.height);
  }

  draw(bitmap, width, height) {
    if (!this.image || this.image.width !== width || this.image.height !== height) {
      this.scratch.width = width;
      this.scratch.height = height;
      this.image = new ImageData(width, height);
    }
    const pixels = this.image.data;
    const rowBytes = (width + 7) >> 3;
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const ink = (bitmap[y * rowBytes + (x >> 3)] >> (x & 7)) & 1;
        const color = ink ? INK : PAPER;
        const p = (y * width + x) * 4;
        pixels[p] = color[0];
        pixels[p + 1] = color[1];
        pixels[p + 2] = color[2];
        pixels[p + 3] = 255;
      }
    }
    this.scratch.getContext("2d").putImageData(this.image, 0, 0);
    const scale = panelScale(width, height);
    const drawWidth = width * scale;
    const drawHeight = height * scale;
    this.clear();
    this.context.imageSmoothingEnabled = false;
    this.context.drawImage(
      this.scratch,
      Math.floor((this.canvas.width - drawWidth) / 2),
      Math.floor((this.canvas.height - drawHeight) / 2),
      drawWidth,
      drawHeight
    );
  }

  text(lines) {
    const { context, canvas } = this;
    this.clear();
    context.fillStyle = `rgb(${INK.join(",")})`;
    context.textAlign = "center";
    context.textBaseline = "middle";
    const [big, small] = lines;
    context.font = "700 96px ui-monospace, Menlo, monospace";
    context.fillText(big, canvas.width / 2, canvas.height / 2 - (small ? 18 : 0));
    if (small) {
      context.font = "600 18px ui-monospace, Menlo, monospace";
      context.fillText(small, canvas.width / 2, canvas.height / 2 + 58);
    }
  }
}

export function loadVideo(video, file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const cleanup = () => {
      video.removeEventListener("loadedmetadata", onLoaded);
      video.removeEventListener("error", onError);
    };
    const onLoaded = () => {
      cleanup();
      resolve({ url, width: video.videoWidth, height: video.videoHeight, duration: video.duration });
    };
    const onError = () => {
      cleanup();
      URL.revokeObjectURL(url);
      reject(new Error("这个浏览器无法解码该视频，请换成 H.264 的 MP4"));
    };
    video.addEventListener("loadedmetadata", onLoaded);
    video.addEventListener("error", onError);
    video.src = url;
    video.load();
  });
}

export function seekTo(video, time) {
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      video.removeEventListener("seeked", onSeeked);
      video.removeEventListener("error", onError);
    };
    const onSeeked = () => {
      cleanup();
      resolve();
    };
    const onError = () => {
      cleanup();
      reject(new Error("视频跳转失败"));
    };
    video.addEventListener("seeked", onSeeked);
    video.addEventListener("error", onError);
    video.currentTime = time;
  });
}

function abortError() {
  return new DOMException("已取消", "AbortError");
}

export function supportsFrameCallbacks() {
  return typeof HTMLVideoElement !== "undefined" && "requestVideoFrameCallback" in HTMLVideoElement.prototype;
}

/**
 * @param {HTMLVideoElement} video  a loaded video element
 * @param {object} options
 *   width, height   frame size (already fitted)
 *   fps             target frame rate
 *   start, end      clip in seconds
 *   bitmap          { mode, threshold, invert } for grayToBitmap
 *   precise         seek frame by frame instead of sampling playback
 *   signal          AbortSignal
 *   onProgress      ({ done, total, bytes, stats }) => void
 *   onFrame         (bitmap) => void, throttled by the caller
 */
export async function convertVideo(video, options) {
  const { width, height, fps, start, end, signal } = options;
  const fpsX100 = Math.round(fps * 100);
  const total = Math.max(1, Math.floor((end - start) * fps + 1e-6));
  const processor = new FrameProcessor(width, height);
  const encoder = new RlaEncoder({ width, height, fpsX100, keyframeInterval: Math.round(fps * 15) });
  const context = { ...options, total, processor, encoder };
  if (signal?.aborted) throw abortError();
  if (options.precise || !supportsFrameCallbacks()) await captureBySeeking(video, context);
  else await captureByPlayback(video, context);
  const bytes = encoder.finish();
  return { bytes, frames: encoder.frameCount, width, height, fpsX100, stats: { ...encoder.stats } };
}

async function captureBySeeking(video, context) {
  const { total, fps, start, processor, encoder, bitmap, signal, onProgress, onFrame } = context;
  video.pause();
  const lastTime = Math.max(0, video.duration - 0.001);
  for (let index = 0; index < total; index += 1) {
    if (signal?.aborted) throw abortError();
    await seekTo(video, Math.min(lastTime, start + index / fps + 0.0005));
    const frame = processor.bitmapFrom(video, bitmap);
    encoder.addFrame(frame);
    onFrame?.(frame);
    if (index % 5 === 0 || index === total - 1) {
      onProgress?.({ done: index + 1, total, bytes: encoder.byteLength, stats: encoder.stats });
    }
  }
}

function captureByPlayback(video, context) {
  const { total, fps, start, end, processor, encoder, bitmap, signal, onProgress, onFrame } = context;
  // Stay well inside what one display refresh can deliver, so no target frame
  // is skipped: about 45 target frames per second of wall time.
  const rate = Math.max(1, Math.min(4, Math.floor(45 / fps)));
  return new Promise((resolve, reject) => {
    let emitted = 0;
    let previous = null;
    let finished = false;
    let handle = 0;

    const emitBefore = (time) => {
      while (emitted < total && start + emitted / fps < time) {
        encoder.addFrame(previous);
        emitted += 1;
      }
    };

    const cleanup = () => {
      finished = true;
      if (handle) video.cancelVideoFrameCallback(handle);
      video.removeEventListener("ended", onEnded);
      video.removeEventListener("pause", onPause);
      document.removeEventListener("visibilitychange", onVisibility);
      signal?.removeEventListener("abort", onAbort);
      video.pause();
      video.playbackRate = 1;
    };

    const complete = () => {
      if (finished) return;
      if (!previous) {
        cleanup();
        reject(new Error("没有解码出任何画面"));
        return;
      }
      emitBefore(Infinity);
      cleanup();
      onProgress?.({ done: emitted, total, bytes: encoder.byteLength, stats: encoder.stats });
      resolve();
    };

    const onFrameCallback = (_now, metadata) => {
      if (finished) return;
      const time = metadata.mediaTime;
      const frame = processor.bitmapFrom(video, bitmap);
      if (!previous) previous = frame;  // the first frame also covers anything before it
      emitBefore(time);
      previous = frame;
      onFrame?.(frame);
      onProgress?.({ done: emitted, total, bytes: encoder.byteLength, stats: encoder.stats });
      if (time >= end || emitted >= total) {
        complete();
        return;
      }
      handle = video.requestVideoFrameCallback(onFrameCallback);
    };

    const onEnded = () => complete();
    const onAbort = () => {
      if (finished) return;
      cleanup();
      reject(abortError());
    };
    // Browsers pause muted background videos; carry on once the tab is back.
    const onPause = () => {
      if (!finished && !document.hidden && !video.ended) video.play().catch(() => {});
    };
    const onVisibility = () => {
      if (!finished && !document.hidden && video.paused && !video.ended) video.play().catch(() => {});
    };

    video.addEventListener("ended", onEnded);
    video.addEventListener("pause", onPause);
    document.addEventListener("visibilitychange", onVisibility);
    signal?.addEventListener("abort", onAbort);

    video.muted = true;
    video.pause();
    seekTo(video, start)
      .then(() => {
        if (finished) return;
        video.playbackRate = rate;
        handle = video.requestVideoFrameCallback(onFrameCallback);
        return video.play();
      })
      .catch((error) => {
        if (finished) return;
        cleanup();
        reject(error);
      });
  });
}

export function suggestAnimationId(fileName) {
  const base = String(fileName || "")
    .replace(/\.[^.]+$/, "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 40);
  return base || "egg";
}

export function sanitizeAnimationId(value) {
  const id = String(value || "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "-")
    .replace(/^[-_]+/, "")
    .slice(0, 48);
  return /^[a-z0-9][a-z0-9_-]{0,47}$/.test(id) ? id : "";
}
