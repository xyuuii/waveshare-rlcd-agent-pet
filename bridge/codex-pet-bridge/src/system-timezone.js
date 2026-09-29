import { readFileSync, readlinkSync } from "node:fs";

// The ESP32 needs a POSIX TZ rule (e.g. "GMT0BST,M3.5.0/1,M10.5.0"), not an
// IANA name. TZif v2+ files carry exactly that rule in their footer, so we read
// it from the Mac's /etc/localtime instead of shipping a zone database.

const POSIX_TZ_PATTERN = /^[A-Za-z<][A-Za-z0-9<>+\-,.:/]{2,62}$/;

export function posixTzFromTzif(buffer) {
  if (!buffer || buffer.length < 44) return "";
  const bytes = Buffer.isBuffer(buffer) ? buffer : Buffer.from(buffer);
  if (bytes.toString("latin1", 0, 4) !== "TZif") return "";
  const version = bytes[4];
  if (version !== 0x32 && version !== 0x33 && version !== 0x34) return "";  // '2', '3', '4'
  const text = bytes.toString("latin1");
  const end = text.lastIndexOf("\n");
  if (end <= 0) return "";
  const start = text.lastIndexOf("\n", end - 1);
  if (start < 0) return "";
  const rule = text.slice(start + 1, end);
  return POSIX_TZ_PATTERN.test(rule) ? rule : "";
}

export function isAcceptablePosixTz(value) {
  return typeof value === "string" && POSIX_TZ_PATTERN.test(value);
}

export function zoneNameFromLocaltimeLink(target) {
  const marker = "zoneinfo/";
  const at = String(target || "").lastIndexOf(marker);
  return at >= 0 ? target.slice(at + marker.length) : "";
}

let cache = { at: 0, value: null };

export function readSystemTimezone({ localtimePath = "/etc/localtime", maxAgeMs = 10 * 60 * 1000, now = Date.now() } = {}) {
  if (cache.value && now - cache.at < maxAgeMs && cache.path === localtimePath) return cache.value;
  let zone = "";
  let posix = "";
  try {
    zone = zoneNameFromLocaltimeLink(readlinkSync(localtimePath));
  } catch {
    zone = "";
  }
  if (!zone) {
    try {
      zone = Intl.DateTimeFormat().resolvedOptions().timeZone || "";
    } catch {
      zone = "";
    }
  }
  try {
    posix = posixTzFromTzif(readFileSync(localtimePath));
  } catch {
    posix = "";
  }
  const value = { zone, posix };
  cache = { at: now, value, path: localtimePath };
  return value;
}
