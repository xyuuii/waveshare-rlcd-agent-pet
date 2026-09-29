#include "anim_codec.h"

#include <string.h>

namespace {

constexpr int kPanelWidth = 400;   // landscape, as the user sees it
constexpr int kPanelHeight = 300;
constexpr int kPanelRowBytes = 38 * 8;  // native tile row (300 px + padding)
constexpr size_t kPanelBytes = static_cast<size_t>(kPanelRowBytes) * 50;

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline void setInk(uint8_t* panel, int x, int y) {
  // Landscape (x, y) -> native (299 - y, x); bytes are vertical, LSB on top.
  const size_t index = static_cast<size_t>(x >> 3) * kPanelRowBytes + static_cast<size_t>(kPanelHeight - 1 - y);
  panel[index] = static_cast<uint8_t>(panel[index] & ~(1u << (x & 7)));
}

}  // namespace

bool parseAnimHeader(const uint8_t* data, size_t length, AnimHeader& out) {
  if (!data || length < kAnimHeaderSize || memcmp(data, "RLA1", 4) != 0) {
    return false;
  }
  AnimHeader header{};
  header.width = readU16(data + 4);
  header.height = readU16(data + 6);
  header.fpsX100 = readU16(data + 8);
  header.flags = readU16(data + 10);
  header.frameCount = readU32(data + 12);
  header.indexOffset = readU32(data + 16);
  if (header.width == 0 || header.width > kAnimMaxWidth || header.height == 0 || header.height > kAnimMaxHeight) {
    return false;
  }
  if (header.fpsX100 < 100 || header.fpsX100 > 6000 || header.frameCount == 0) {
    return false;
  }
  out = header;
  return true;
}

bool unpackBits(const uint8_t* src, size_t srcLength, uint8_t* out, size_t outLength, bool xorInto) {
  if (!out || (!src && srcLength > 0)) {
    return false;
  }
  size_t in = 0;
  size_t produced = 0;
  while (produced < outLength) {
    if (in >= srcLength) {
      return false;
    }
    const int8_t control = static_cast<int8_t>(src[in++]);
    if (control >= 0) {
      const size_t count = static_cast<size_t>(control) + 1;
      if (in + count > srcLength || produced + count > outLength) {
        return false;
      }
      if (xorInto) {
        for (size_t i = 0; i < count; ++i) {
          out[produced + i] ^= src[in + i];
        }
      } else {
        memcpy(out + produced, src + in, count);
      }
      in += count;
      produced += count;
    } else if (control != -128) {
      const size_t count = static_cast<size_t>(1 - control);
      if (in >= srcLength || produced + count > outLength) {
        return false;
      }
      const uint8_t value = src[in++];
      if (xorInto) {
        if (value != 0) {
          for (size_t i = 0; i < count; ++i) {
            out[produced + i] ^= value;
          }
        }
      } else {
        memset(out + produced, value, count);
      }
      produced += count;
    }
  }
  return in == srcLength;
}

bool applyAnimFrame(uint8_t type, const uint8_t* payload, size_t payloadLength, uint8_t* frame, size_t frameLength) {
  switch (type) {
    case kAnimFrameKeyRaw:
      if (payloadLength != frameLength || !payload) {
        return false;
      }
      memcpy(frame, payload, frameLength);
      return true;
    case kAnimFrameKeyRle:
      return unpackBits(payload, payloadLength, frame, frameLength, false);
    case kAnimFrameDeltaRle:
      return unpackBits(payload, payloadLength, frame, frameLength, true);
    case kAnimFrameRepeat:
      return payloadLength == 0;
    default:
      return false;
  }
}

size_t readAnimRecord(const uint8_t* data,
                      size_t length,
                      uint8_t& type,
                      const uint8_t*& payload,
                      size_t& payloadLength) {
  if (!data || length < kAnimRecordHeaderSize) {
    return 0;
  }
  const size_t size = readU16(data + 2);
  if (kAnimRecordHeaderSize + size > length) {
    return 0;
  }
  type = data[0];
  payload = data + kAnimRecordHeaderSize;
  payloadLength = size;
  return kAnimRecordHeaderSize + size;
}

int animFitScale(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) {
    return 1;
  }
  const int sx = kPanelWidth / width;
  const int sy = kPanelHeight / height;
  const int scale = sx < sy ? sx : sy;
  return scale < 1 ? 1 : scale;
}

void blitAnimFrameToPanel(const uint8_t* frame, uint16_t width, uint16_t height, int scale, uint8_t* panel) {
  if (!frame || !panel || width == 0 || height == 0) {
    return;
  }
  if (scale < 1) {
    scale = 1;
  }
  const size_t stride = animStride(width);
  if (width == kPanelWidth && height == kPanelHeight && scale == 1) {
    // Fast path: a landscape byte (8 horizontal pixels) is exactly one native byte.
    for (int y = 0; y < kPanelHeight; ++y) {
      const uint8_t* row = frame + static_cast<size_t>(y) * stride;
      const size_t column = static_cast<size_t>(kPanelHeight - 1 - y);
      for (size_t xb = 0; xb < stride; ++xb) {
        panel[xb * kPanelRowBytes + column] = static_cast<uint8_t>(~row[xb]);
      }
    }
    return;
  }

  memset(panel, 0xFF, kPanelBytes);
  const int outWidth = width * scale;
  const int outHeight = height * scale;
  const int clipWidth = outWidth < kPanelWidth ? outWidth : kPanelWidth;
  const int clipHeight = outHeight < kPanelHeight ? outHeight : kPanelHeight;
  const int offsetX = (kPanelWidth - clipWidth) / 2;
  const int offsetY = (kPanelHeight - clipHeight) / 2;
  for (int oy = 0; oy < clipHeight; ++oy) {
    const int sy = oy / scale;
    const uint8_t* row = frame + static_cast<size_t>(sy) * stride;
    for (int ox = 0; ox < clipWidth; ++ox) {
      const int sx = ox / scale;
      if ((row[sx >> 3] >> (sx & 7)) & 1) {
        setInk(panel, offsetX + ox, offsetY + oy);
      }
    }
  }
}
