// Where a video frame lands on the 400x300 panel: size presets, black border
// detection and fit/fill cropping. No DOM here, so the tests can run in Node.

export const PANEL_WIDTH = 400;
export const PANEL_HEIGHT = 300;

export const SIZE_PRESETS = {
  full: { maxWidth: 400, maxHeight: 300 },
  half: { maxWidth: 200, maxHeight: 150 }
};

export function fitSize(sourceWidth, sourceHeight, maxWidth, maxHeight) {
  if (!sourceWidth || !sourceHeight) return { width: maxWidth, height: maxHeight };
  const scale = Math.min(maxWidth / sourceWidth, maxHeight / sourceHeight);
  return {
    width: Math.max(8, Math.min(maxWidth, Math.round(sourceWidth * scale))),
    height: Math.max(8, Math.min(maxHeight, Math.round(sourceHeight * scale)))
  };
}

// Same integer scale the firmware picks (anim_codec.cpp animFitScale).
export function panelScale(width, height) {
  return Math.max(1, Math.min(Math.floor(PANEL_WIDTH / width), Math.floor(PANEL_HEIGHT / height)));
}

/**
 * Picks the part of the source frame to convert and the output size.
 *   content  the picture inside any black borders ({x, y, w, h} in source
 *            pixels), or null for the whole frame
 *   framing  "fill": cover the whole output, cropping what sticks out;
 *            "fit": show all of it, leaving margins the board fills with paper
 * Returns { width, height, rect } with rect in source pixels.
 */
export function frameLayout(sourceWidth, sourceHeight, { content = null, framing = "fill", maxWidth = PANEL_WIDTH, maxHeight = PANEL_HEIGHT } = {}) {
  const box = content || { x: 0, y: 0, w: sourceWidth, h: sourceHeight };
  if (!box.w || !box.h) return { width: maxWidth, height: maxHeight, rect: null };
  if (framing === "fit") {
    const { width, height } = fitSize(box.w, box.h, maxWidth, maxHeight);
    return { width, height, rect: { ...box } };
  }
  const target = maxWidth / maxHeight;
  let { x, y, w, h } = box;
  if (w / h > target) {
    const cropped = h * target;
    x += (w - cropped) / 2;
    w = cropped;
  } else {
    const cropped = w / target;
    y += (h - cropped) / 2;
    h = cropped;
  }
  return { width: maxWidth, height: maxHeight, rect: { x, y, w, h } };
}

/**
 * Finds black borders from the brightest value each row and column reached
 * over several sampled frames (0-255). Rows and columns that stayed dark in
 * every sample are border; the picture keeps everything else. Borders are
 * trimmed evenly on opposite sides (letterbox and pillarbox are symmetric),
 * so a dark object near one edge cannot eat into the picture.
 * Returns { left, top, right, bottom } (exclusive right/bottom) in the
 * analysed grid, or null when there is nothing worth trimming.
 */
export function findContentBox(rowMax, colMax, { threshold = 40, minKeep = 0.5, minTrim = 0.015, inset = 1 } = {}) {
  const height = rowMax.length;
  const width = colMax.length;
  const lead = (values) => {
    const index = values.findIndex((value) => value > threshold);
    return index < 0 ? values.length : index;
  };
  const trail = (values) => {
    let count = 0;
    for (let i = values.length - 1; i >= 0 && !(values[i] > threshold); i -= 1) count += 1;
    return count;
  };
  let trimY = Math.min(lead(rowMax), trail(rowMax));
  let trimX = Math.min(lead(colMax), trail(colMax));
  // Scaled video blurs the edge of a border: skip one more line.
  if (trimY > 0) trimY += inset;
  if (trimX > 0) trimX += inset;
  const keptHeight = height - 2 * trimY;
  const keptWidth = width - 2 * trimX;
  // Mostly dark everywhere: a dark video, not borders.
  if (keptHeight < height * minKeep || keptWidth < width * minKeep) return null;
  if (trimY < height * minTrim) trimY = 0;
  if (trimX < width * minTrim) trimX = 0;
  if (!trimX && !trimY) return null;
  return { left: trimX, top: trimY, right: width - trimX, bottom: height - trimY };
}

// Maps a box found on an analysis grid back to source pixels.
export function scaleBox(box, gridWidth, gridHeight, sourceWidth, sourceHeight) {
  const sx = sourceWidth / gridWidth;
  const sy = sourceHeight / gridHeight;
  const x = Math.round(box.left * sx);
  const y = Math.round(box.top * sy);
  return { x, y, w: Math.round(box.right * sx) - x, h: Math.round(box.bottom * sy) - y };
}

// For the status line: how much of each side the borders took.
export function describeTrim(content, sourceWidth, sourceHeight) {
  if (!content) return "";
  const side = Math.round(((sourceWidth - content.w) / 2 / sourceWidth) * 100);
  const top = Math.round(((sourceHeight - content.h) / 2 / sourceHeight) * 100);
  const parts = [];
  if (side > 0) parts.push(`左右各 ${side}%`);
  if (top > 0) parts.push(`上下各 ${top}%`);
  return parts.join("、");
}
