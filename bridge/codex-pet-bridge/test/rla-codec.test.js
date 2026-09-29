import assert from "node:assert/strict";
import test from "node:test";

import {
  FRAME_DELTA_RLE,
  FRAME_KEY_RLE,
  FRAME_REPEAT,
  RlaEncoder,
  decodeFrames,
  frameBytes,
  grayToBitmap,
  packBits,
  parseRla,
  recordsChunk,
  rgbaToGray,
  stride,
  unpackBits
} from "../src/rla-codec.js";
import { boxAnimation } from "../tools/gen-rla-vectors.mjs";

function randomBytes(length, seed) {
  let state = seed;
  const out = new Uint8Array(length);
  for (let i = 0; i < length; i += 1) {
    state = (state * 1103515245 + 12345) >>> 0;
    out[i] = (state >>> 16) & 0xff;
  }
  return out;
}

test("packbits round-trips runs, literals and edge lengths", () => {
  const cases = [
    new Uint8Array(0),
    Uint8Array.of(7),
    new Uint8Array(129).fill(0xaa),
    new Uint8Array(300).fill(0),
    randomBytes(1000, 1),
    Uint8Array.from([...new Array(130).fill(1), 2, 3, 3, 4, ...new Array(5).fill(9)])
  ];
  for (const input of cases) {
    const packed = packBits(input);
    assert.deepEqual(unpackBits(packed, input.length), input);
  }
  assert.ok(packBits(new Uint8Array(15000)).length < 300);
});

test("packbits decoder rejects truncated and oversized input", () => {
  assert.throws(() => unpackBits(Uint8Array.of(5, 1, 2), 8), /literal overflow/);
  assert.throws(() => unpackBits(Uint8Array.of(0xf0, 0x11), 8), /run overflow/);
  assert.throws(() => unpackBits(Uint8Array.of(0xf9, 0x11, 0), 8), /trailing/);
  assert.throws(() => unpackBits(Uint8Array.of(), 1), /truncated/);
});

test("encoder picks repeat, delta and key frames and the file decodes back", () => {
  const width = 400;
  const height = 300;
  const encoder = new RlaEncoder({ width, height, fpsX100: 3000, keyframeInterval: 0 });
  const frames = [];
  // A static textured band makes key frames expensive, so deltas should win.
  const texture = randomBytes(stride(width) * 80, 7);
  for (let index = 0; index < 12; index += 1) {
    const bitmap = new Uint8Array(frameBytes(width, height));
    bitmap.set(texture, 0);
    const cx = 50 + index * 20;
    for (let y = 100; y < 200; y += 1) {
      for (let x = cx; x < cx + 60; x += 1) bitmap[y * stride(width) + (x >> 3)] |= 1 << (x & 7);
    }
    frames.push(bitmap);
    encoder.addFrame(bitmap);
    if (index === 5) {
      frames.push(bitmap);
      encoder.addFrame(bitmap);
    }
  }
  const bytes = encoder.finish();
  const parsed = parseRla(bytes);
  assert.equal(parsed.header.frameCount, frames.length);
  assert.equal(bytes[parsed.offsets[0]], FRAME_KEY_RLE);
  assert.equal(bytes[parsed.offsets[6]], FRAME_REPEAT);
  assert.equal(bytes[parsed.offsets[1]], FRAME_DELTA_RLE);
  assert.ok(bytes.length < frames[0].length * 2, `compressed size ${bytes.length}`);
  let index = 0;
  for (const frame of decodeFrames(bytes)) {
    assert.deepEqual(frame, frames[index], `frame ${index}`);
    index += 1;
  }
  assert.equal(index, frames.length);
});

test("parse rejects corrupted files", () => {
  const { bytes } = boxAnimation();
  const badMagic = bytes.slice();
  badMagic[0] = 0x58;
  assert.throws(() => parseRla(badMagic), /not an RLA1/);
  assert.throws(() => parseRla(bytes.subarray(0, bytes.length - 1)), /index table/);
  const badType = bytes.slice();
  badType[parseRla(bytes).offsets[2]] = 9;
  assert.throws(() => parseRla(badType), /unknown type/);
  const huge = bytes.slice();
  new DataView(huge.buffer).setUint16(4, 4000, true);
  assert.throws(() => parseRla(huge), /frame size/);
});

test("records chunk honours byte budget but always makes progress", () => {
  const { bytes } = boxAnimation();
  const parsed = parseRla(bytes);
  const all = recordsChunk(bytes, parsed, 0, 100, 1 << 20);
  assert.equal(all.count, parsed.header.frameCount);
  assert.equal(all.bytes.length, parsed.header.indexOffset - 32);
  const tiny = recordsChunk(bytes, parsed, 2, 10, 1);
  assert.equal(tiny.count, 1);
  assert.equal(tiny.start, 2);
  const past = recordsChunk(bytes, parsed, 99, 5, 1000);
  assert.equal(past.count, 0);
});

test("gray conversion thresholds, inverts and dithers", () => {
  const width = 8;
  const height = 4;
  const gray = new Uint8Array(width * height);
  gray.fill(0, 0, 16);
  gray.fill(255, 16);
  const bitmap = grayToBitmap(gray, width, height);
  assert.deepEqual(Array.from(bitmap), [0xff, 0xff, 0x00, 0x00]);
  assert.deepEqual(Array.from(grayToBitmap(gray, width, height, { invert: true })), [0x00, 0x00, 0xff, 0xff]);
  const mid = new Uint8Array(width * height).fill(128);
  const dithered = grayToBitmap(mid, width, height, { mode: "dither" });
  const inkCount = Array.from(dithered).reduce((sum, byte) => sum + byte.toString(2).split("1").length - 1, 0);
  assert.equal(inkCount, 16);
  const rgba = Uint8ClampedArray.of(255, 0, 0, 255, 0, 0, 255, 255);
  assert.deepEqual(Array.from(rgbaToGray(rgba, 2, 1)), [76, 29]);
});
