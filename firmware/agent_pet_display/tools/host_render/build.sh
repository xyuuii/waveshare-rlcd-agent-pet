#!/usr/bin/env bash
# Build and run the host renderer: real firmware drawing code + real U8g2,
# rendered into the panel's native buffer, dumped as 400x300 images.
#
#   tools/host_render/build.sh [output-dir]
#
# U8G2_DIR may point at a U8g2 "src" directory. By default the copy that
# PlatformIO downloads for the firmware (.pio/libdeps/waveshare_rlcd) is used.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$FW/.pio/host_render}"
U8G2_DIR="${U8G2_DIR:-$FW/.pio/libdeps/waveshare_rlcd/U8g2/src}"
BUILD="${HOST_RENDER_BUILD:-$FW/.pio/host_render_build}"
CC="${CC:-cc}"
CXX="${CXX:-c++}"

if [[ ! -f "$U8G2_DIR/U8g2lib.h" ]]; then
  echo "U8g2 sources not found at $U8G2_DIR (run 'pio pkg install -e waveshare_rlcd' or set U8G2_DIR)" >&2
  exit 2
fi

mkdir -p "$BUILD/u8g2" "$OUT"

# U8g2 is large (the font file alone is ~40 MB of C); compile it once.
if [[ ! -f "$BUILD/u8g2/libu8g2host.a" ]]; then
  echo "compiling U8g2 (one time)..."
  pids=()
  for f in "$U8G2_DIR"/clib/*.c; do
    "$CC" -c -O1 -ffunction-sections -fdata-sections -w -I"$U8G2_DIR/clib" "$f" \
      -o "$BUILD/u8g2/$(basename "${f%.c}").o" &
    pids+=($!)
  done
  for pid in "${pids[@]}"; do wait "$pid"; done
  "$CXX" -c -O1 -std=gnu++17 -w -I"$U8G2_DIR" -I"$U8G2_DIR/clib" "$U8G2_DIR/U8g2lib.cpp" -o "$BUILD/u8g2/U8g2lib.o"
  "$CXX" -c -O1 -std=gnu++17 -w -I"$U8G2_DIR" -I"$U8G2_DIR/clib" "$U8G2_DIR/U8x8lib.cpp" -o "$BUILD/u8g2/U8x8lib.o"
  ar rcs "$BUILD/u8g2/libu8g2host.a" "$BUILD"/u8g2/*.o
fi

FW_SOURCES=(
  "$FW/src/screen_renderer.cpp"
  "$FW/src/screen_layout.cpp"
  "$FW/src/doto_font.cpp"
  "$FW/src/pet_sprites.cpp"
  "$FW/src/usage_visuals.cpp"
  "$FW/src/pet_state_machine.cpp"
)
for optional in clock_model.cpp clock_faces.cpp anim_codec.cpp egg_builtin.cpp; do
  if [[ -f "$FW/src/$optional" ]]; then
    FW_SOURCES+=("$FW/src/$optional")
  fi
done

HARNESS_SOURCES=(
  "$HERE/host_display.cpp"
  "$HERE/host_frame.cpp"
  "$HERE/render_scenes.cpp"
)

LINK_GC="-Wl,--gc-sections"
if [[ "$(uname -s)" == "Darwin" ]]; then
  LINK_GC="-Wl,-dead_strip"
fi

"$CXX" -O1 -g -std=gnu++17 -Wall -Wextra -Wno-unused-parameter \
  -DHOST_RENDER=1 \
  -I"$HERE/shim" -I"$FW/include" -I"$HERE" -I"$U8G2_DIR" -I"$U8G2_DIR/clib" \
  "${FW_SOURCES[@]}" "${HARNESS_SOURCES[@]}" \
  "$BUILD/u8g2/libu8g2host.a" $LINK_GC -lm \
  -o "$BUILD/render_scenes"

"$BUILD/render_scenes" "$OUT"

if command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" >/dev/null 2>&1; then
  python3 "$HERE/pbm_to_png.py" "$OUT"
fi
