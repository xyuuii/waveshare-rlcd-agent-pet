#include <unity.h>

#include <string.h>

#include <vector>

#include "anim_codec.h"
#include "anim_vectors.h"

namespace {

// Reference PackBits encoder (same rules as the JS encoder).
std::vector<uint8_t> packBits(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out;
  size_t i = 0;
  while (i < data.size()) {
    size_t run = 1;
    while (i + run < data.size() && run < 128 && data[i + run] == data[i]) {
      ++run;
    }
    if (run >= 2) {
      out.push_back(static_cast<uint8_t>(static_cast<int8_t>(1 - static_cast<int>(run))));
      out.push_back(data[i]);
      i += run;
      continue;
    }
    size_t start = i;
    size_t literal = 0;
    while (i < data.size() && literal < 128) {
      if (i + 1 < data.size() && data[i] == data[i + 1]) {
        break;
      }
      ++i;
      ++literal;
    }
    out.push_back(static_cast<uint8_t>(literal - 1));
    out.insert(out.end(), data.begin() + static_cast<long>(start), data.begin() + static_cast<long>(start + literal));
  }
  return out;
}

bool panelBitIsPaper(const uint8_t* panel, int x, int y) {
  const size_t index = static_cast<size_t>(x >> 3) * 304 + static_cast<size_t>(299 - y);
  return (panel[index] >> (x & 7)) & 1;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_packbits_round_trip_and_xor(void) {
  std::vector<uint8_t> data(15000, 0);
  for (size_t i = 3000; i < 3200; ++i) {
    data[i] = static_cast<uint8_t>(i * 37);
  }
  memset(&data[8000], 0xFF, 700);
  const std::vector<uint8_t> packed = packBits(data);
  TEST_ASSERT_TRUE(packed.size() < data.size() / 10);
  std::vector<uint8_t> out(15000, 0xAA);
  TEST_ASSERT_TRUE(unpackBits(packed.data(), packed.size(), out.data(), out.size(), false));
  TEST_ASSERT_EQUAL_MEMORY(data.data(), out.data(), data.size());

  // XOR mode: applying the same delta twice restores the original.
  std::vector<uint8_t> frame(data);
  TEST_ASSERT_TRUE(unpackBits(packed.data(), packed.size(), frame.data(), frame.size(), true));
  for (uint8_t value : frame) {
    TEST_ASSERT_EQUAL_HEX8(0, value);
  }
}

void test_packbits_rejects_bad_input(void) {
  uint8_t out[8] = {0};
  const uint8_t truncatedLiteral[] = {0x05, 1, 2};
  TEST_ASSERT_FALSE(unpackBits(truncatedLiteral, sizeof(truncatedLiteral), out, sizeof(out), false));
  const uint8_t overflow[] = {0xF0, 0x11};  // repeat 17 into 8 bytes
  TEST_ASSERT_FALSE(unpackBits(overflow, sizeof(overflow), out, sizeof(out), false));
  const uint8_t trailing[] = {0xF9, 0x11, 0x00, 0x22};  // 8 bytes + junk
  TEST_ASSERT_FALSE(unpackBits(trailing, sizeof(trailing), out, sizeof(out), false));
  const uint8_t exact[] = {0x80, 0xF9, 0x11};  // no-op then repeat 8
  TEST_ASSERT_TRUE(unpackBits(exact, sizeof(exact), out, sizeof(out), false));
  TEST_ASSERT_EQUAL_HEX8(0x11, out[7]);
  TEST_ASSERT_FALSE(unpackBits(nullptr, 4, out, sizeof(out), false));
}

void test_frame_types(void) {
  uint8_t frame[4] = {1, 2, 3, 4};
  const uint8_t raw[4] = {9, 9, 9, 9};
  TEST_ASSERT_TRUE(applyAnimFrame(kAnimFrameKeyRaw, raw, 4, frame, 4));
  TEST_ASSERT_EQUAL_HEX8(9, frame[0]);
  TEST_ASSERT_FALSE(applyAnimFrame(kAnimFrameKeyRaw, raw, 3, frame, 4));
  TEST_ASSERT_TRUE(applyAnimFrame(kAnimFrameRepeat, nullptr, 0, frame, 4));
  TEST_ASSERT_FALSE(applyAnimFrame(kAnimFrameRepeat, raw, 1, frame, 4));
  const uint8_t delta[] = {0xFD, 0x0F};  // xor 0x0F into 4 bytes
  TEST_ASSERT_TRUE(applyAnimFrame(kAnimFrameDeltaRle, delta, sizeof(delta), frame, 4));
  TEST_ASSERT_EQUAL_HEX8(0x06, frame[3]);
  TEST_ASSERT_FALSE(applyAnimFrame(7, delta, sizeof(delta), frame, 4));
}

void test_record_reader(void) {
  const uint8_t chunk[] = {2, 0, 2, 0, 0xFD, 0x0F, 3, 0, 0, 0, 1, 0, 9};
  uint8_t type = 0xEE;
  const uint8_t* payload = nullptr;
  size_t length = 0;
  size_t used = readAnimRecord(chunk, sizeof(chunk), type, payload, length);
  TEST_ASSERT_EQUAL(6, static_cast<int>(used));
  TEST_ASSERT_EQUAL(2, type);
  TEST_ASSERT_EQUAL(2, static_cast<int>(length));
  used += readAnimRecord(chunk + used, sizeof(chunk) - used, type, payload, length);
  TEST_ASSERT_EQUAL(10, static_cast<int>(used));
  TEST_ASSERT_EQUAL(3, type);
  TEST_ASSERT_EQUAL(0, static_cast<int>(length));
  // Third record claims 9 bytes of payload but only 1 is present.
  const uint8_t truncated[] = {1, 0, 9, 0, 1};
  TEST_ASSERT_EQUAL(0, static_cast<int>(readAnimRecord(truncated, sizeof(truncated), type, payload, length)));
}

void test_header_validation(void) {
  uint8_t header[32] = {'R', 'L', 'A', '1'};
  header[4] = 400 & 0xFF;
  header[5] = 400 >> 8;
  header[6] = 300 & 0xFF;
  header[7] = 300 >> 8;
  header[8] = 3000 & 0xFF;
  header[9] = 3000 >> 8;
  header[12] = 10;
  AnimHeader parsed{};
  TEST_ASSERT_TRUE(parseAnimHeader(header, sizeof(header), parsed));
  TEST_ASSERT_EQUAL(400, parsed.width);
  TEST_ASSERT_EQUAL(3000, parsed.fpsX100);
  TEST_ASSERT_EQUAL_UINT32(10, parsed.frameCount);
  header[4] = 0xFF;
  header[5] = 0xFF;  // absurd width
  TEST_ASSERT_FALSE(parseAnimHeader(header, sizeof(header), parsed));
  TEST_ASSERT_FALSE(parseAnimHeader(header, 16, parsed));
}

void test_blit_places_pixels_upright(void) {
  static uint8_t frame[15000];
  static uint8_t fast[15200];
  static uint8_t slow[15200];
  memset(frame, 0, sizeof(frame));
  // Ink at the four corners and one interior pixel.
  const int points[5][2] = {{0, 0}, {399, 0}, {0, 299}, {399, 299}, {123, 45}};
  for (const auto& point : points) {
    frame[point[1] * 50 + point[0] / 8] |= static_cast<uint8_t>(1 << (point[0] & 7));
  }
  memset(fast, 0xFF, sizeof(fast));
  blitAnimFrameToPanel(frame, 400, 300, 1, fast);
  for (const auto& point : points) {
    TEST_ASSERT_FALSE(panelBitIsPaper(fast, point[0], point[1]));
  }
  TEST_ASSERT_TRUE(panelBitIsPaper(fast, 1, 0));
  TEST_ASSERT_TRUE(panelBitIsPaper(fast, 200, 150));

  // The generic path must agree with the fast path.
  memset(slow, 0x00, sizeof(slow));
  // Force the generic path with a 1-pixel-narrower frame, then compare the shared area.
  static uint8_t narrow[(399 + 7) / 8 * 300];
  memset(narrow, 0, sizeof(narrow));
  narrow[45 * 50 + 123 / 8] |= static_cast<uint8_t>(1 << (123 & 7));
  blitAnimFrameToPanel(narrow, 399, 300, 1, slow);
  // Centered: (400 - 399) / 2 = 0 offset, so pixel stays at 123,45.
  TEST_ASSERT_FALSE(panelBitIsPaper(slow, 123, 45));
  TEST_ASSERT_TRUE(panelBitIsPaper(slow, 124, 45));
}

void test_small_frames_scale_and_center(void) {
  TEST_ASSERT_EQUAL(2, animFitScale(200, 150));
  TEST_ASSERT_EQUAL(1, animFitScale(400, 300));
  TEST_ASSERT_EQUAL(3, animFitScale(128, 96));
  static uint8_t panel[15200];
  uint8_t tiny[2] = {0x01, 0x00};  // 8x2 frame, ink at (0,0)
  blitAnimFrameToPanel(tiny, 8, 2, 2, panel);
  // 16x4 output centered at (192,148); ink covers 2x2 at the top-left.
  TEST_ASSERT_FALSE(panelBitIsPaper(panel, 192, 148));
  TEST_ASSERT_FALSE(panelBitIsPaper(panel, 193, 149));
  TEST_ASSERT_TRUE(panelBitIsPaper(panel, 194, 148));
  TEST_ASSERT_TRUE(panelBitIsPaper(panel, 0, 0));
}

void test_vectors_from_js_encoder_decode(void) {
  AnimHeader header{};
  TEST_ASSERT_TRUE(parseAnimHeader(kAnimVectorFile, sizeof(kAnimVectorFile), header));
  TEST_ASSERT_EQUAL(kAnimVectorWidth, header.width);
  TEST_ASSERT_EQUAL(kAnimVectorHeight, header.height);
  TEST_ASSERT_EQUAL_UINT32(kAnimVectorFrames, header.frameCount);
  const size_t frameBytes = animFrameBytes(header.width, header.height);
  TEST_ASSERT_EQUAL(static_cast<int>(sizeof(kAnimVectorExpected)), static_cast<int>(frameBytes * header.frameCount));
  std::vector<uint8_t> frame(frameBytes, 0);
  size_t offset = kAnimHeaderSize;
  bool sawDelta = false;
  bool sawRepeat = false;
  for (uint32_t index = 0; index < header.frameCount; ++index) {
    uint8_t type = 0;
    const uint8_t* payload = nullptr;
    size_t length = 0;
    const size_t used = readAnimRecord(kAnimVectorFile + offset, header.indexOffset - offset, type, payload, length);
    TEST_ASSERT_TRUE(used > 0);
    TEST_ASSERT_TRUE(applyAnimFrame(type, payload, length, frame.data(), frame.size()));
    sawDelta = sawDelta || type == kAnimFrameDeltaRle;
    sawRepeat = sawRepeat || type == kAnimFrameRepeat;
    TEST_ASSERT_EQUAL_MEMORY(kAnimVectorExpected + index * frameBytes, frame.data(), frameBytes);
    offset += used;
  }
  TEST_ASSERT_EQUAL(static_cast<int>(header.indexOffset), static_cast<int>(offset));
  TEST_ASSERT_TRUE(sawDelta);
  TEST_ASSERT_TRUE(sawRepeat);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_packbits_round_trip_and_xor);
  RUN_TEST(test_packbits_rejects_bad_input);
  RUN_TEST(test_frame_types);
  RUN_TEST(test_record_reader);
  RUN_TEST(test_header_validation);
  RUN_TEST(test_blit_places_pixels_upright);
  RUN_TEST(test_small_frames_scale_and_center);
  RUN_TEST(test_vectors_from_js_encoder_decode);
  return UNITY_END();
}
