// RLA1 — 1-bit animation container for the RLCD pet display.
//
// Shared by the bridge (Node), the dashboard (browser) and mirrored in C++ by
// firmware/agent_pet_display/src/anim_codec.cpp. Keep the three in sync.
//
// Header (32 bytes, little endian)
//   0  "RLA1"          4  u16 width      6  u16 height
//   8  u16 fps x100    10 u16 flags (bit0: 1 = ink)
//   12 u32 frame count 16 u32 index offset (one u32 file offset per frame)
//   20..31 reserved
// Frame record: u8 type, u8 reserved, u16 payload length, payload
// Bitmap: row-major, stride = ceil(width / 8), bit (x & 7), 1 = ink (dark).

export const RLA_MAGIC = "RLA1";
export const HEADER_SIZE = 32;
export const RECORD_HEADER_SIZE = 4;
export const FRAME_KEY_RAW = 0;
export const FRAME_KEY_RLE = 1;
export const FRAME_DELTA_RLE = 2;
export const FRAME_REPEAT = 3;
export const MAX_WIDTH = 400;
export const MAX_HEIGHT = 300;
export const FLAG_INK = 1;

export function stride(width) {
  return (width + 7) >> 3;
}

export function frameBytes(width, height) {
  return stride(width) * height;
}

// Apple PackBits: n in 0..127 -> n+1 literals; n in -127..-1 -> repeat next byte 1-n times.
export function packBits(bytes) {
  const out = [];
  let i = 0;
  const length = bytes.length;
  while (i < length) {
    let run = 1;
    while (i + run < length && run < 128 && bytes[i + run] === bytes[i]) run += 1;
    if (run >= 2) {
      out.push((257 - run) & 0xff);
      out.push(bytes[i]);
      i += run;
      continue;
    }
    const start = i;
    let literal = 0;
    while (i < length && literal < 128) {
      if (i + 1 < length && bytes[i] === bytes[i + 1]) break;
      i += 1;
      literal += 1;
    }
    out.push(literal - 1);
    for (let k = start; k < start + literal; k += 1) out.push(bytes[k]);
  }
  return Uint8Array.from(out);
}

export function unpackBits(src, outLength, out = new Uint8Array(outLength), xorInto = false) {
  let input = 0;
  let produced = 0;
  while (produced < outLength) {
    if (input >= src.length) throw new Error("packbits: truncated input");
    const control = (src[input] << 24) >> 24;
    input += 1;
    if (control >= 0) {
      const count = control + 1;
      if (input + count > src.length || produced + count > outLength) throw new Error("packbits: literal overflow");
      for (let k = 0; k < count; k += 1) {
        out[produced + k] = xorInto ? out[produced + k] ^ src[input + k] : src[input + k];
      }
      input += count;
      produced += count;
    } else if (control !== -128) {
      const count = 1 - control;
      if (input >= src.length || produced + count > outLength) throw new Error("packbits: run overflow");
      const value = src[input];
      input += 1;
      for (let k = 0; k < count; k += 1) {
        out[produced + k] = xorInto ? out[produced + k] ^ value : value;
      }
      produced += count;
    }
  }
  if (input !== src.length) throw new Error("packbits: trailing bytes");
  return out;
}

function writeU16(view, offset, value) {
  view.setUint16(offset, value, true);
}

function writeU32(view, offset, value) {
  view.setUint32(offset, value, true);
}

export class RlaEncoder {
  constructor({ width, height, fpsX100, keyframeInterval = 300 }) {
    if (!Number.isInteger(width) || width < 1 || width > MAX_WIDTH) throw new Error(`bad width ${width}`);
    if (!Number.isInteger(height) || height < 1 || height > MAX_HEIGHT) throw new Error(`bad height ${height}`);
    if (!Number.isInteger(fpsX100) || fpsX100 < 100 || fpsX100 > 6000) throw new Error(`bad fps ${fpsX100}`);
    this.width = width;
    this.height = height;
    this.fpsX100 = fpsX100;
    this.keyframeInterval = keyframeInterval;
    this.frameLength = frameBytes(width, height);
    this.previous = null;
    this.records = [];
    this.byteLength = HEADER_SIZE;
    this.stats = { key: 0, delta: 0, repeat: 0, raw: 0 };
  }

  addFrame(bitmap) {
    if (!(bitmap instanceof Uint8Array) || bitmap.length !== this.frameLength) {
      throw new Error(`frame must be a Uint8Array of ${this.frameLength} bytes`);
    }
    const index = this.records.length;
    const forceKey = this.previous === null || (this.keyframeInterval > 0 && index % this.keyframeInterval === 0);
    let type;
    let payload;
    if (!forceKey && equalBytes(bitmap, this.previous)) {
      type = FRAME_REPEAT;
      payload = new Uint8Array(0);
      this.stats.repeat += 1;
    } else {
      const key = packBits(bitmap);
      type = FRAME_KEY_RLE;
      payload = key;
      if (!forceKey) {
        const diff = new Uint8Array(this.frameLength);
        for (let k = 0; k < this.frameLength; k += 1) diff[k] = bitmap[k] ^ this.previous[k];
        const delta = packBits(diff);
        if (delta.length < key.length) {
          type = FRAME_DELTA_RLE;
          payload = delta;
        }
      }
      if (payload.length >= this.frameLength) {
        type = FRAME_KEY_RAW;
        payload = bitmap.slice();
      }
      if (type === FRAME_DELTA_RLE) this.stats.delta += 1;
      else if (type === FRAME_KEY_RAW) this.stats.raw += 1;
      else this.stats.key += 1;
    }
    this.previous = bitmap.slice();
    this.records.push({ type, payload });
    this.byteLength += RECORD_HEADER_SIZE + payload.length;
  }

  get frameCount() {
    return this.records.length;
  }

  finish() {
    if (this.records.length === 0) throw new Error("no frames");
    const indexOffset = this.byteLength;
    const total = indexOffset + this.records.length * 4;
    const bytes = new Uint8Array(total);
    const view = new DataView(bytes.buffer);
    bytes.set([0x52, 0x4c, 0x41, 0x31], 0);
    writeU16(view, 4, this.width);
    writeU16(view, 6, this.height);
    writeU16(view, 8, this.fpsX100);
    writeU16(view, 10, FLAG_INK);
    writeU32(view, 12, this.records.length);
    writeU32(view, 16, indexOffset);
    let offset = HEADER_SIZE;
    this.records.forEach((record, index) => {
      writeU32(view, indexOffset + index * 4, offset);
      bytes[offset] = record.type;
      bytes[offset + 1] = 0;
      writeU16(view, offset + 2, record.payload.length);
      bytes.set(record.payload, offset + RECORD_HEADER_SIZE);
      offset += RECORD_HEADER_SIZE + record.payload.length;
    });
    return bytes;
  }
}

function equalBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let k = 0; k < a.length; k += 1) if (a[k] !== b[k]) return false;
  return true;
}

// Validates a whole file and returns its header plus per-frame offsets.
export function parseRla(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new Error("expected Uint8Array");
  if (bytes.length < HEADER_SIZE) throw new Error("file too short");
  if (String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) !== RLA_MAGIC) throw new Error("not an RLA1 file");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const header = {
    width: view.getUint16(4, true),
    height: view.getUint16(6, true),
    fpsX100: view.getUint16(8, true),
    flags: view.getUint16(10, true),
    frameCount: view.getUint32(12, true),
    indexOffset: view.getUint32(16, true)
  };
  if (header.width < 1 || header.width > MAX_WIDTH || header.height < 1 || header.height > MAX_HEIGHT) {
    throw new Error("unsupported frame size");
  }
  if (header.fpsX100 < 100 || header.fpsX100 > 6000) throw new Error("unsupported frame rate");
  if (header.frameCount < 1) throw new Error("no frames");
  if (header.indexOffset < HEADER_SIZE || header.indexOffset + header.frameCount * 4 !== bytes.length) {
    throw new Error("index table does not match file size");
  }
  const offsets = new Array(header.frameCount);
  let expected = HEADER_SIZE;
  for (let index = 0; index < header.frameCount; index += 1) {
    const offset = view.getUint32(header.indexOffset + index * 4, true);
    if (offset !== expected) throw new Error(`frame ${index} offset mismatch`);
    if (offset + RECORD_HEADER_SIZE > header.indexOffset) throw new Error(`frame ${index} header out of range`);
    const type = bytes[offset];
    const length = view.getUint16(offset + 2, true);
    if (type > FRAME_REPEAT) throw new Error(`frame ${index} has unknown type ${type}`);
    if (index === 0 && type !== FRAME_KEY_RAW && type !== FRAME_KEY_RLE) throw new Error("first frame must be a key frame");
    offsets[index] = offset;
    expected = offset + RECORD_HEADER_SIZE + length;
    if (expected > header.indexOffset) throw new Error(`frame ${index} payload out of range`);
  }
  if (expected !== header.indexOffset) throw new Error("records do not end at the index table");
  return { header, offsets };
}

// Concatenated frame records [start, start+count), capped by maxBytes but always
// at least one record so a client can make progress.
export function recordsChunk(bytes, parsed, start, count, maxBytes = 16384) {
  const { header, offsets } = parsed;
  const first = Math.max(0, Math.min(header.frameCount, start));
  const last = Math.min(header.frameCount, first + Math.max(0, count));
  if (first >= last) return { bytes: new Uint8Array(0), start: first, count: 0 };
  let end = first;
  let size = 0;
  while (end < last) {
    const recordEnd = end + 1 < header.frameCount ? offsets[end + 1] : header.indexOffset;
    const recordSize = recordEnd - offsets[end];
    if (end > first && size + recordSize > maxBytes) break;
    size += recordSize;
    end += 1;
  }
  const from = offsets[first];
  return { bytes: bytes.subarray(from, from + size), start: first, count: end - first };
}

export function* decodeFrames(bytes) {
  const parsed = parseRla(bytes);
  const { header, offsets } = parsed;
  const length = frameBytes(header.width, header.height);
  const frame = new Uint8Array(length);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let index = 0; index < header.frameCount; index += 1) {
    const offset = offsets[index];
    const type = bytes[offset];
    const size = view.getUint16(offset + 2, true);
    const payload = bytes.subarray(offset + RECORD_HEADER_SIZE, offset + RECORD_HEADER_SIZE + size);
    if (type === FRAME_KEY_RAW) {
      if (size !== length) throw new Error(`frame ${index}: raw size mismatch`);
      frame.set(payload);
    } else if (type === FRAME_KEY_RLE) {
      unpackBits(payload, length, frame, false);
    } else if (type === FRAME_DELTA_RLE) {
      unpackBits(payload, length, frame, true);
    } else if (size !== 0) {
      throw new Error(`frame ${index}: repeat with payload`);
    }
    yield frame;
  }
}

// Grayscale (0..255, one byte per pixel) -> 1-bit bitmap (1 = ink).
// mode: "threshold" or "dither" (4x4 ordered Bayer, for photos / gradients).
const BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

export function grayToBitmap(gray, width, height, { threshold = 128, invert = false, mode = "threshold" } = {}) {
  const out = new Uint8Array(frameBytes(width, height));
  const rowBytes = stride(width);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const value = gray[y * width + x];
      let limit = threshold;
      if (mode === "dither") limit = (BAYER4[(y & 3) * 4 + (x & 3)] + 0.5) * 16;
      let ink = value < limit;
      if (invert) ink = !ink;
      if (ink) out[y * rowBytes + (x >> 3)] |= 1 << (x & 7);
    }
  }
  return out;
}

// RGBA (canvas ImageData) -> grayscale using Rec. 601 luma.
export function rgbaToGray(rgba, width, height) {
  const gray = new Uint8Array(width * height);
  for (let i = 0, p = 0; i < gray.length; i += 1, p += 4) {
    gray[i] = (rgba[p] * 299 + rgba[p + 1] * 587 + rgba[p + 2] * 114) / 1000;
  }
  return gray;
}
