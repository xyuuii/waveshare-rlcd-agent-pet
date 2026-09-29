import { mkdir, readdir, readFile, rename, rm, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { ANIM_ID_PATTERN } from "./display-settings.js";
import { parseRla, recordsChunk } from "./rla-codec.js";

// Stores RLA1 animations uploaded from the dashboard. Files stay on this Mac;
// the board streams them in small chunks and never keeps a copy.
export class AnimStore {
  constructor(dir, { maxBytes = 48 * 1024 * 1024 } = {}) {
    this.dir = dir;
    this.maxBytes = maxBytes;
    this.cache = null;  // { id, mtimeMs, bytes, parsed } — the one being streamed
    this.meta = new Map();  // id -> { mtimeMs, size, description }; listing never re-reads unchanged files
  }

  pathFor(id) {
    if (!ANIM_ID_PATTERN.test(id)) throw Object.assign(new Error("invalid animation id"), { status: 400 });
    return join(this.dir, `${id}.rla`);
  }

  async list() {
    let names = [];
    try {
      names = await readdir(this.dir);
    } catch {
      return [];
    }
    const items = [];
    const seen = new Set();
    for (const name of names) {
      if (!name.endsWith(".rla")) continue;
      const id = name.slice(0, -4);
      if (!ANIM_ID_PATTERN.test(id)) continue;
      seen.add(id);
      try {
        items.push(await this.describe(id));
      } catch {
        // skip unreadable or invalid files
      }
    }
    for (const id of this.meta.keys()) if (!seen.has(id)) this.meta.delete(id);
    return items.sort((a, b) => a.id.localeCompare(b.id));
  }

  async load(id) {
    const path = this.pathFor(id);
    const info = await stat(path).catch(() => null);
    if (!info) throw Object.assign(new Error("animation not found"), { status: 404 });
    if (this.cache && this.cache.id === id && this.cache.mtimeMs === info.mtimeMs) return this.cache;
    const bytes = new Uint8Array(await readFile(path));
    const parsed = parseRla(bytes);
    this.cache = { id, mtimeMs: info.mtimeMs, bytes, parsed };
    return this.cache;
  }

  async describe(id) {
    const info = await stat(this.pathFor(id)).catch(() => null);
    if (!info) throw Object.assign(new Error("animation not found"), { status: 404 });
    const known = this.meta.get(id);
    if (known && known.mtimeMs === info.mtimeMs && known.size === info.size) return known.description;
    const { parsed, bytes, mtimeMs } = await this.load(id);
    const description = describe(id, parsed.header, bytes.length, mtimeMs);
    this.meta.set(id, { mtimeMs: info.mtimeMs, size: info.size, description });
    return description;
  }

  async put(id, bytes) {
    const path = this.pathFor(id);
    if (!(bytes instanceof Uint8Array) || bytes.length === 0) {
      throw Object.assign(new Error("empty upload"), { status: 400 });
    }
    if (bytes.length > this.maxBytes) throw Object.assign(new Error("animation too large"), { status: 413 });
    let parsed;
    try {
      parsed = parseRla(bytes);
    } catch (error) {
      throw Object.assign(new Error(`invalid RLA1 file: ${error.message}`), { status: 400 });
    }
    await mkdir(this.dir, { recursive: true });
    const tmp = `${path}.${process.pid}.tmp`;
    await writeFile(tmp, bytes);
    await rename(tmp, path);
    this.cache = null;
    const info = await stat(path);
    return describe(id, parsed.header, bytes.length, info.mtimeMs);
  }

  async remove(id) {
    await rm(this.pathFor(id), { force: true });
    if (this.cache?.id === id) this.cache = null;
    this.meta.delete(id);
  }

  async chunk(id, start, count, maxBytes) {
    const { bytes, parsed } = await this.load(id);
    return recordsChunk(bytes, parsed, start, count, maxBytes);
  }
}

function describe(id, header, size, mtimeMs) {
  return {
    id,
    width: header.width,
    height: header.height,
    fpsX100: header.fpsX100,
    frames: header.frameCount,
    durationS: Math.round((header.frameCount * 10000) / header.fpsX100) / 100,
    bytes: size,
    updatedAt: new Date(mtimeMs).toISOString()
  };
}
