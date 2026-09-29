#!/usr/bin/env bash
# Checks the board <-> bridge contract with the real code on both sides:
# starts the bridge, sets a clock face, uploads a small RLA1 animation and
# schedules it, then feeds the bridge's /esp32/poll answer to the firmware's
# own parser and decodes the streamed frame chunk with the firmware's codec.
#
#   tools/contract-test.sh
#
# ARDUINOJSON_DIR defaults to the copy PlatformIO downloads for [env:native].
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW="$ROOT/firmware/agent_pet_display"
BRIDGE="$ROOT/bridge/codex-pet-bridge"
ARDUINOJSON_DIR="${ARDUINOJSON_DIR:-$FW/.pio/libdeps/native/ArduinoJson}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pet-contract.XXXXXX")"
PORT=$((17700 + $$ % 90))
TOKEN="contract-$$"
SERVER_PID=""

cleanup() {
  [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

[[ -f "$ARDUINOJSON_DIR/src/ArduinoJson.h" ]] || { echo "ArduinoJson not found at $ARDUINOJSON_DIR" >&2; exit 2; }

printf '%s\n' "$TOKEN" > "$WORK/token"
(cd "$BRIDGE" && PET_BRIDGE_TOKEN="" PET_BRIDGE_TOKEN_FILE="$WORK/token" PET_BRIDGE_PORT="$PORT" \
  PET_BRIDGE_LOG="$WORK/events.jsonl" PET_BRIDGE_STATE="$WORK/bridge-state.json" \
  PET_BRIDGE_CLAUDE_PROJECTS="$WORK/none" PET_BRIDGE_CODEX_STATE="$WORK/none.sqlite" \
  node ./src/bridge-server.js > "$WORK/bridge.log" 2>&1) &
SERVER_PID=$!

# Drive the bridge like the dashboard does and save what the board would get.
(cd "$BRIDGE" && node --input-type=module - "$PORT" "$TOKEN" "$WORK" <<'JS'
import { writeFileSync } from "node:fs";
import { decodeFrames } from "./src/rla-codec.js";
import { boxAnimation } from "./tools/gen-rla-vectors.mjs";

const [port, token, work] = process.argv.slice(2);
const base = `http://127.0.0.1:${port}`;
const auth = { authorization: `Bearer ${token}` };
for (let i = 0; i < 50; i++) {
  try { await fetch(`${base}/health`); break; } catch { await new Promise((r) => setTimeout(r, 100)); }
}
const json = (body) => ({ method: "POST", headers: { ...auth, "content-type": "application/json" }, body: JSON.stringify(body) });
await fetch(`${base}/settings`, json({ clockStyle: "analog", hour12: true, showSeconds: false, page: "clock", tzOverride: "GMT0BST,M3.5.0/1,M10.5.0" }));
const { bytes } = boxAnimation();
const put = await fetch(`${base}/anim/demo`, { method: "PUT", headers: { ...auth, "content-type": "application/octet-stream" }, body: bytes });
if (put.status !== 201) throw new Error(`upload failed ${put.status}`);
await fetch(`${base}/egg/play`, json({ id: "demo", delayMs: 5000 }));
const poll = await fetch(`${base}/esp32/poll?token=${token}&fw=1.3.0&page=overview`);
writeFileSync(`${work}/poll.json`, await poll.text());
const frames = [];
for (const frame of decodeFrames(bytes)) frames.push(Buffer.from(frame));
writeFileSync(`${work}/expected.bin`, Buffer.concat(frames));
JS
)

cat > "$WORK/harness.cpp" <<'CPP'
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "anim_codec.h"
#include "bridge_client.h"

static int failures = 0;
#define CHECK(cond, what)                                   \
  do {                                                      \
    if (cond) {                                             \
      std::printf("  ok   %s\n", what);                     \
    } else {                                                \
      std::printf("  FAIL %s\n", what);                     \
      ++failures;                                           \
    }                                                       \
  } while (0)

static std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  const std::string work = argv[1];
  const std::string pollUrl = argv[2];
  std::vector<uint8_t> json = readFile(work + "/poll.json");
  json.push_back(0);

  AgentState state{};
  BridgeCommands commands{};
  CHECK(parsePollPayload(reinterpret_cast<const char*>(json.data()), state, commands), "firmware parses /esp32/poll");
  CHECK(commands.serverTimeMs > 1700000000000LL, "server time present");
  CHECK(commands.display.present && commands.display.rev > 0, "display command present");
  CHECK(commands.display.hasStyle && commands.display.style == ClockStyle::Analog, "clock face = analog");
  CHECK(commands.display.hasHour12 && commands.display.hour12, "12-hour clock");
  CHECK(commands.display.hasShowSeconds && !commands.display.showSeconds, "seconds hidden");
  CHECK(commands.display.hasPage && commands.display.page == ScreenPage::Clock, "page switch to clock");
  CHECK(commands.hasTz && commands.tz == "GMT0BST,M3.5.0/1,M10.5.0", "time zone rule");
  CHECK(commands.egg.present && commands.egg.id == "demo", "egg command present");
  CHECK(commands.egg.frames == 7 && commands.egg.fpsX100 == 1000, "egg frames and rate");
  CHECK(commands.egg.width == 64 && commands.egg.height == 24, "egg frame size");
  CHECK(commands.egg.startAtMs > commands.serverTimeMs, "egg starts after the poll");

  // The URL the network task would request for the first chunk.
  std::printf("%s\n", buildAnimChunkUrl(pollUrl.c_str(), commands.egg.id, 0, commands.egg.frames, 16384).c_str());
  return failures == 0 ? 0 : 1;
}
CPP

cat > "$WORK/decode.cpp" <<'CPP'
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "anim_codec.h"

int main(int argc, char** argv) {
  auto read = [](const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const std::vector<uint8_t> chunk = read(argv[1]);
  const std::vector<uint8_t> expected = read(argv[2]);
  const size_t frameLength = animFrameBytes(64, 24);
  std::vector<uint8_t> frame(frameLength, 0);
  size_t offset = 0;
  size_t index = 0;
  int failures = 0;
  while (offset < chunk.size()) {
    uint8_t type = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLength = 0;
    const size_t used = readAnimRecord(chunk.data() + offset, chunk.size() - offset, type, payload, payloadLength);
    if (used == 0 || !applyAnimFrame(type, payload, payloadLength, frame.data(), frameLength)) {
      std::printf("  FAIL record %zu does not decode\n", index);
      return 1;
    }
    if (std::memcmp(frame.data(), expected.data() + index * frameLength, frameLength) != 0) {
      std::printf("  FAIL frame %zu differs from the bridge's own decoder\n", index);
      ++failures;
    }
    offset += used;
    ++index;
  }
  std::printf("  %s streamed chunk decodes to the same %zu frames as the bridge\n", failures ? "FAIL" : "ok  ", index);
  return failures == 0 && index == expected.size() / frameLength ? 0 : 1;
}
CPP

CXXFLAGS=(-std=gnu++17 -O0 -w -DPIO_UNIT_TESTING -I"$FW/include" -I"$ARDUINOJSON_DIR/src")
SOURCES=("$FW/src/bridge_client.cpp" "$FW/src/clock_model.cpp" "$FW/src/anim_codec.cpp" "$FW/src/pet_state_machine.cpp"
         "$FW/src/focus_controller.cpp" "$FW/src/time_keeper.cpp")
c++ "${CXXFLAGS[@]}" "$WORK/harness.cpp" "${SOURCES[@]}" -o "$WORK/harness"
c++ "${CXXFLAGS[@]}" "$WORK/decode.cpp" "$FW/src/anim_codec.cpp" -o "$WORK/decode"

echo "board protocol"
CHUNK_URL="$("$WORK/harness" "$WORK" "http://127.0.0.1:$PORT/esp32/poll?token=$TOKEN" | tee /dev/stderr | tail -1)"
echo "frame streaming"
curl -sS -o "$WORK/chunk.bin" -w '  chunk: HTTP %{http_code}, %{size_download} bytes\n' "$CHUNK_URL"
"$WORK/decode" "$WORK/chunk.bin" "$WORK/expected.bin"
echo "contract ok"
