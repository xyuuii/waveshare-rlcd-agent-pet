#pragma once

#include <stddef.h>
#include <stdint.h>

// "RLA1" — a tiny 1-bit animation container shared with the bridge and the
// dashboard encoder (bridge/codex-pet-bridge/ui/rla-codec.js).
//
// Header (32 bytes, little endian):
//   0  "RLA1"   4  u16 width   6  u16 height   8  u16 fps x100   10 u16 flags
//   12 u32 frame count        16 u32 index offset (u32 per frame, from file start)
//   20..31 reserved
// Frame record: u8 type, u8 reserved, u16 payload length, payload.
// Frame bitmap: row-major, stride = ceil(width / 8), bit (x & 7) of each byte,
// 1 = ink (dark). Payloads are PackBits; deltas are XORed onto the last frame.

static constexpr uint8_t kAnimFrameKeyRaw = 0;
static constexpr uint8_t kAnimFrameKeyRle = 1;
static constexpr uint8_t kAnimFrameDeltaRle = 2;
static constexpr uint8_t kAnimFrameRepeat = 3;
static constexpr size_t kAnimHeaderSize = 32;
static constexpr size_t kAnimRecordHeaderSize = 4;
static constexpr uint16_t kAnimMaxWidth = 400;
static constexpr uint16_t kAnimMaxHeight = 300;
static constexpr size_t kAnimMaxFrameBytes = (kAnimMaxWidth / 8) * kAnimMaxHeight;  // 15000

struct AnimHeader {
  uint16_t width{0};
  uint16_t height{0};
  uint16_t fpsX100{0};
  uint16_t flags{0};
  uint32_t frameCount{0};
  uint32_t indexOffset{0};
};

inline size_t animStride(uint16_t width) {
  return (static_cast<size_t>(width) + 7) / 8;
}

inline size_t animFrameBytes(uint16_t width, uint16_t height) {
  return animStride(width) * height;
}

bool parseAnimHeader(const uint8_t* data, size_t length, AnimHeader& out);

// PackBits decode of exactly outLength bytes. With xorInto the bytes are XORed
// into out (delta frames). Returns false on malformed or truncated input.
bool unpackBits(const uint8_t* src, size_t srcLength, uint8_t* out, size_t outLength, bool xorInto);

// Applies one frame record to the running frame buffer.
bool applyAnimFrame(uint8_t type, const uint8_t* payload, size_t payloadLength, uint8_t* frame, size_t frameLength);

// Reads one record from a chunk. Returns bytes consumed, 0 if incomplete.
size_t readAnimRecord(const uint8_t* data,
                      size_t length,
                      uint8_t& type,
                      const uint8_t*& payload,
                      size_t& payloadLength);

// Integer scale that fits a frame into the 400x300 panel (at least 1).
int animFitScale(uint16_t width, uint16_t height);

// Writes a frame into the RLCD's native U8g2 buffer (300x400 portrait,
// vertical-LSB bytes, 38 tiles wide, bit 1 = paper) so it appears upright in
// the 400x300 landscape view. Frames are centered and scaled by `scale`.
void blitAnimFrameToPanel(const uint8_t* frame, uint16_t width, uint16_t height, int scale, uint8_t* panel);
