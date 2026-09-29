#include "egg_player.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "anim_codec.h"
#include "egg_builtin.h"

namespace {

enum class Mode {
  Idle,
  Builtin,
  Countdown,
  Playing,
  Message,
};

constexpr uint32_t kBuiltinFrameMs = 50;
constexpr uint32_t kStallTimeoutMs = 6000;
constexpr uint32_t kMessageMs = 2500;
constexpr uint32_t kDecodeBudgetMs = 25;

Mode gMode = Mode::Idle;
uint32_t gStartMs = 0;
uint32_t gLastFrameMs = 0;
uint32_t gMessageUntilMs = 0;
int gLastCountdown = -1;
BridgeEggCommand gCommand{};
int gScale = 1;
uint32_t gDecoded = 0;  // frames decoded so far
AnimChunkView gChunk{};
bool gHaveChunk = false;
size_t gChunkOffset = 0;
uint32_t gWaitingSinceMs = 0;
bool gBadData = false;
uint8_t gFrame[kAnimMaxFrameBytes];

void showMessage(U8G2& g, const char* line1, const char* line2, uint32_t nowMs) {
  drawEggMessage(g, line1, line2);
  g.sendBuffer();
  gMode = Mode::Message;
  gMessageUntilMs = nowMs + kMessageMs;
}

void releaseChunk() {
  if (gHaveChunk) {
    netWorkerReleaseChunk(gChunk.slot);
    gHaveChunk = false;
  }
}

// Decodes forward until `target` frames are decoded or data runs out.
bool decodeUpTo(uint32_t target) {
  bool advanced = false;
  const uint32_t began = millis();
  const size_t frameLength = animFrameBytes(gCommand.width, gCommand.height);
  while (gDecoded < target) {
    if (!gHaveChunk) {
      if (!netWorkerPeekChunk(gDecoded, gChunk)) {
        break;
      }
      gHaveChunk = true;
      gChunkOffset = 0;
    }
    uint8_t type = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLength = 0;
    const size_t used = readAnimRecord(gChunk.data + gChunkOffset, gChunk.length - gChunkOffset, type, payload,
                                       payloadLength);
    if (used == 0 || !applyAnimFrame(type, payload, payloadLength, gFrame, frameLength)) {
      Serial.printf("[EGG] bad record at frame %lu\n", static_cast<unsigned long>(gDecoded));
      releaseChunk();
      gBadData = true;  // the stream cannot continue past a broken frame
      return false;
    }
    gChunkOffset += used;
    gDecoded += 1;
    advanced = true;
    if (gChunkOffset >= gChunk.length) {
      releaseChunk();
    }
    if (millis() - began > kDecodeBudgetMs) {
      break;  // keep buttons responsive; catch up next tick
    }
  }
  return advanced;
}

}  // namespace

void eggStartBuiltin(uint32_t nowMs) {
  eggStop();
  gMode = Mode::Builtin;
  gStartMs = nowMs;
  gLastFrameMs = 0;
  Serial.println("[EGG] builtin");
}

void eggStartStream(const BridgeEggCommand& command, const NetSnapshot& snapshot, uint32_t nowMs) {
  eggStop();
  gCommand = command;
  gScale = animFitScale(command.width, command.height);
  gDecoded = 0;
  gWaitingSinceMs = 0;
  gBadData = false;
  gLastCountdown = -1;
  int64_t startLocal = static_cast<int64_t>(nowMs) + 2000;
  if (snapshot.serverOffsetKnown && command.startAtMs > 0) {
    startLocal = command.startAtMs - snapshot.serverOffsetMs;
  }
  // Leave time to buffer, but never make the user wait forever.
  if (startLocal < static_cast<int64_t>(nowMs) + 800) {
    startLocal = static_cast<int64_t>(nowMs) + 800;
  }
  if (startLocal > static_cast<int64_t>(nowMs) + 15000) {
    startLocal = static_cast<int64_t>(nowMs) + 15000;
  }
  gStartMs = static_cast<uint32_t>(startLocal);
  memset(gFrame, 0, sizeof(gFrame));
  netWorkerStartStream(command.id, command.frames);
  gMode = Mode::Countdown;
  Serial.printf("[EGG] stream id=%s frames=%lu fps=%u.%02u size=%ux%u scale=%d\n",
                command.id.c_str(),
                static_cast<unsigned long>(command.frames),
                command.fpsX100 / 100,
                command.fpsX100 % 100,
                command.width,
                command.height,
                gScale);
}

void eggStop() {
  if (gMode == Mode::Countdown || gMode == Mode::Playing) {
    releaseChunk();
    netWorkerStopStream();
  }
  gHaveChunk = false;
  gMode = Mode::Idle;
}

bool eggActive() {
  return gMode != Mode::Idle;
}

void eggTick(U8G2& g, uint32_t nowMs) {
  switch (gMode) {
    case Mode::Idle:
      return;

    case Mode::Message:
      if (static_cast<int32_t>(nowMs - gMessageUntilMs) >= 0) {
        gMode = Mode::Idle;
      }
      return;

    case Mode::Builtin: {
      const uint32_t elapsed = nowMs - gStartMs;
      if (elapsed >= builtinEggDurationMs()) {
        gMode = Mode::Idle;
        return;
      }
      if (gLastFrameMs == 0 || nowMs - gLastFrameMs >= kBuiltinFrameMs) {
        drawBuiltinEggFrame(g, elapsed);
        g.sendBuffer();
        gLastFrameMs = nowMs;
      }
      return;
    }

    case Mode::Countdown: {
      if (netWorkerStreamFailed()) {
        // Nothing to stream after all: fall back to the offline animation.
        Serial.println("[EGG] stream failed before start, playing builtin");
        eggStartBuiltin(nowMs);
        return;
      }
      const int32_t remaining = static_cast<int32_t>(gStartMs - nowMs);
      if (remaining <= 0) {
        gMode = Mode::Playing;
        gWaitingSinceMs = 0;
        return;
      }
      const int seconds = (remaining + 999) / 1000;
      if (seconds != gLastCountdown) {
        drawEggCountdown(g, gCommand.id.c_str(), seconds);
        g.sendBuffer();
        gLastCountdown = seconds;
      }
      return;
    }

    case Mode::Playing: {
      const uint32_t elapsed = nowMs - gStartMs;
      uint32_t target = static_cast<uint32_t>((static_cast<uint64_t>(elapsed) * gCommand.fpsX100) / 100000ULL) + 1;
      if (target > gCommand.frames) {
        target = gCommand.frames;
      }
      const bool advanced = decodeUpTo(target);
      if (gBadData) {
        eggStop();
        showMessage(g, "BAD ANIMATION DATA", "UPLOAD IT AGAIN", nowMs);
        return;
      }
      if (advanced) {
        blitAnimFrameToPanel(gFrame, gCommand.width, gCommand.height, gScale, g.getBufferPtr());
        g.sendBuffer();
        gWaitingSinceMs = 0;
      } else if (gDecoded < target) {
        if (gWaitingSinceMs == 0) {
          gWaitingSinceMs = nowMs;
        } else if (nowMs - gWaitingSinceMs > kStallTimeoutMs || netWorkerStreamFailed()) {
          eggStop();
          showMessage(g, "STREAM STALLED", "CHECK THE BRIDGE / WIFI", nowMs);
          return;
        }
      }
      const uint32_t durationMs =
          static_cast<uint32_t>((static_cast<uint64_t>(gCommand.frames) * 100000ULL) / gCommand.fpsX100);
      if (gDecoded >= gCommand.frames && elapsed >= durationMs + 400) {
        eggStop();
      }
      return;
    }
  }
}
