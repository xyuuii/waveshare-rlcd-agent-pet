#!/usr/bin/env bash
# Runs the native Unity tests without PlatformIO (same sources as [env:native]).
# Canonical command remains: pio test -e native
#
#   tools/native_tests.sh [test-name ...]
#
# UNITY_DIR / ARDUINOJSON_DIR default to the copies PlatformIO downloads.
set -euo pipefail

FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNITY_DIR="${UNITY_DIR:-$FW/.pio/libdeps/native/Unity}"
ARDUINOJSON_DIR="${ARDUINOJSON_DIR:-$FW/.pio/libdeps/native/ArduinoJson}"
BUILD="${NATIVE_TEST_BUILD:-$FW/.pio/native_tests_build}"
CC="${CC:-cc}"
CXX="${CXX:-c++}"

# Keep in sync with build_src_filter of [env:native] in platformio.ini.
SOURCES=(
  bridge_client.cpp battery_monitor.cpp button_actions.cpp doto_font.cpp
  focus_controller.cpp pet_sprites.cpp pet_state_machine.cpp screen_layout.cpp
  usage_visuals.cpp clock_model.cpp time_keeper.cpp device_settings.cpp anim_codec.cpp
)

mkdir -p "$BUILD"
"$CC" -c -O0 -I"$UNITY_DIR/src" "$UNITY_DIR/src/unity.c" -o "$BUILD/unity.o"

SRC_PATHS=()
for source in "${SOURCES[@]}"; do
  if [[ -f "$FW/src/$source" ]]; then
    SRC_PATHS+=("$FW/src/$source")
  fi
done

selected=("$@")
total=0
failed=0
for dir in "$FW"/test/native/*/; do
  name="$(basename "$dir")"
  if [[ ${#selected[@]} -gt 0 && ! " ${selected[*]} " =~ " $name " ]]; then
    continue
  fi
  if ! "$CXX" -std=gnu++17 -O0 -g -Wall -Wextra -Wno-unused-parameter -DPIO_UNIT_TESTING \
      -I"$FW/include" -I"$UNITY_DIR/src" -I"$ARDUINOJSON_DIR/src" \
      "$dir"/*.cpp "${SRC_PATHS[@]}" "$BUILD/unity.o" -o "$BUILD/$name" 2>"$BUILD/$name.log"; then
    echo "$name: BUILD FAILED"
    sed -n '1,40p' "$BUILD/$name.log"
    failed=$((failed + 1))
    continue
  fi
  if output="$("$BUILD/$name" 2>&1)"; then
    summary="$(printf '%s\n' "$output" | grep -E '^[0-9]+ Tests' || true)"
    echo "$name: $summary"
  else
    echo "$name: FAILED"
    printf '%s\n' "$output" | grep -E ':FAIL|Tests' || printf '%s\n' "$output" | tail -20
    failed=$((failed + 1))
    summary="$(printf '%s\n' "$output" | grep -E '^[0-9]+ Tests' || true)"
  fi
  count="$(printf '%s\n' "$summary" | awk '{print $1}')"
  total=$((total + ${count:-0}))
done

echo "native tests: $total run, $failed suite(s) failing"
[[ $failed -eq 0 ]]
