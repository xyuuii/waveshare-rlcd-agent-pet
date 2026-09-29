// Remembers what the RLCD board reported in its last /esp32/poll query.

function int(params, key) {
  const raw = params.get(key);
  if (raw === null || raw === "") return null;
  const value = Number(raw);
  return Number.isFinite(value) ? Math.trunc(value) : null;
}

function float(params, key) {
  const raw = params.get(key);
  if (raw === null || raw === "") return null;
  const value = Number(raw);
  return Number.isFinite(value) ? value : null;
}

function text(params, key, max = 32) {
  const raw = params.get(key);
  return raw ? String(raw).slice(0, max) : "";
}

export function parseTelemetry(params) {
  return {
    firmware: text(params, "fw", 24),
    battery: int(params, "bat"),
    batteryMv: int(params, "mv"),
    charging: params.get("chg") === "1",
    temperatureC: float(params, "temp"),
    humidity: float(params, "hum"),
    rssi: int(params, "rssi"),
    uptimeS: int(params, "up"),
    page: text(params, "page", 16),
    clockStyle: text(params, "style", 16),
    settingsRev: int(params, "srev"),
    eggRev: int(params, "erev"),
    freeHeap: int(params, "heap"),
    timeValid: params.get("clk") === "1",
    // Counter of secret gestures since the board booted; null when not reported.
    eggRequest: int(params, "eggreq")
  };
}

export class DeviceRegistry {
  constructor({ onlineWindowMs = 15_000 } = {}) {
    this.onlineWindowMs = onlineWindowMs;
    this.devices = new Map();
    this.lastAddress = "";
  }

  // Returns { eggRequested } when the board's secret-gesture counter moved.
  // The counter restarts at 0 when the board reboots, so any change to a
  // non-zero value counts; the first sighting after a bridge restart never does.
  update(telemetry, { address = "unknown", now = Date.now() } = {}) {
    const previous = this.devices.get(address);
    const reported = Number.isInteger(telemetry.eggRequest) ? telemetry.eggRequest : null;
    const known = previous ? previous.lastEggRequest : null;
    const eggRequested = Boolean(previous) && reported !== null && reported > 0 && reported !== known;
    this.devices.set(address, {
      address,
      firstSeenAt: previous?.firstSeenAt || now,
      lastSeenAt: now,
      polls: (previous?.polls || 0) + 1,
      lastEggRequest: reported !== null ? reported : known,
      telemetry
    });
    this.lastAddress = address;
    return { eggRequested };
  }

  snapshot(now = Date.now()) {
    const device = this.devices.get(this.lastAddress);
    if (!device) return { seen: false, online: false };
    const ageMs = now - device.lastSeenAt;
    return {
      seen: true,
      online: ageMs <= this.onlineWindowMs,
      address: device.address,
      lastSeenAt: new Date(device.lastSeenAt).toISOString(),
      ageMs,
      polls: device.polls,
      ...device.telemetry
    };
  }
}
