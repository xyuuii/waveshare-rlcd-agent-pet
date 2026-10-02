import assert from "node:assert/strict";
import { test } from "node:test";

import { describeTrim, findContentBox, fitSize, frameLayout, scaleBox } from "../ui/framing.js";

// Brightest value per column/row over the sampled frames: `dark` at the
// borders, bright picture in between.
function profile(length, darkLead, darkTrail, { dark = 18, bright = 255 } = {}) {
  return Uint8Array.from({ length }, (_, i) => (i < darkLead || i >= length - darkTrail ? dark : bright));
}

test("fit keeps the whole 16:9 picture and leaves margins", () => {
  assert.deepEqual(fitSize(1920, 1080, 400, 300), { width: 400, height: 225 });
  const layout = frameLayout(1920, 1080, { framing: "fit" });
  assert.deepEqual([layout.width, layout.height], [400, 225]);
  assert.deepEqual(layout.rect, { x: 0, y: 0, w: 1920, h: 1080 });
});

test("fill covers 400x300 and crops the sides of a 16:9 picture", () => {
  const layout = frameLayout(1920, 1080, { framing: "fill" });
  assert.deepEqual([layout.width, layout.height], [400, 300]);
  assert.deepEqual(layout.rect, { x: 240, y: 0, w: 1440, h: 1080 });
});

test("fill crops top and bottom of a portrait picture", () => {
  const layout = frameLayout(1080, 1920, { framing: "fill", maxWidth: 200, maxHeight: 150 });
  assert.deepEqual([layout.width, layout.height], [200, 150]);
  assert.equal(layout.rect.w, 1080);
  assert.equal(layout.rect.h, 810);
  assert.equal(layout.rect.y, 555);
});

test("a 4:3 picture pillarboxed in 16:9 fills the panel once the borders are trimmed", () => {
  const content = { x: 240, y: 0, w: 1440, h: 1080 };
  for (const framing of ["fit", "fill"]) {
    const layout = frameLayout(1920, 1080, { content, framing });
    assert.deepEqual([layout.width, layout.height], [400, 300], framing);
    assert.deepEqual(layout.rect, content, framing);
  }
});

test("finds pillarbox borders and trims one extra column for the blurred edge", () => {
  const box = findContentBox(profile(270, 0, 0), profile(480, 60, 60));
  assert.deepEqual(box, { left: 61, top: 0, right: 419, bottom: 270 });
  const source = scaleBox(box, 480, 270, 3840, 2160);
  assert.deepEqual(source, { x: 488, y: 0, w: 2864, h: 2160 });
  assert.equal(describeTrim(source, 3840, 2160), "左右各 13%");
});

test("finds letterbox borders", () => {
  const box = findContentBox(profile(270, 33, 33), profile(480, 0, 0));
  assert.deepEqual(box, { left: 0, top: 34, right: 480, bottom: 236 });
});

test("trims only what is dark on both sides", () => {
  // A dark object along the left edge is not a border on its own.
  const box = findContentBox(profile(270, 0, 0), profile(480, 60, 3));
  assert.equal(box, null, "3 px on the right is too little to count");
  const wide = findContentBox(profile(270, 0, 0), profile(480, 60, 30));
  assert.deepEqual(wide, { left: 31, top: 0, right: 449, bottom: 270 });
});

test("a dark video is not mistaken for borders", () => {
  assert.equal(findContentBox(profile(270, 100, 100), profile(480, 200, 200)), null);
  assert.equal(findContentBox(new Uint8Array(270), new Uint8Array(480)), null);
});

test("no borders, nothing to trim", () => {
  assert.equal(findContentBox(profile(270, 0, 0), profile(480, 0, 0)), null);
  assert.equal(findContentBox(profile(270, 1, 1), profile(480, 2, 2)), null, "a hairline is ignored");
  assert.equal(describeTrim(null, 1920, 1080), "");
});
