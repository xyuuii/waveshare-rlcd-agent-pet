#!/bin/bash
# Builds PetBar.app with the Xcode Command Line Tools (no Xcode project).
#
#   ./build.sh              build into ./build/PetBar.app
#   ./build.sh --install    also copy it to ~/Applications and start it
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/build"
APP="$OUT/PetBar.app"
VERSION="1.0.0"
INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "PetBar is a macOS app; build it on the Mac." >&2
  exit 1
fi
if ! xcrun --sdk macosx --find swiftc >/dev/null 2>&1; then
  echo "swiftc not found. Install the Command Line Tools first: xcode-select --install" >&2
  exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
xcrun --sdk macosx swiftc -O -swift-version 5 \
  -target "$(uname -m)-apple-macos13.0" \
  -framework AppKit -framework ServiceManagement \
  "$HERE/main.swift" -o "$APP/Contents/MacOS/PetBar"
sed "s/__VERSION__/$VERSION/" "$HERE/Info.plist" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null
# Ad-hoc signature: enough for a local app and for "open at login".
codesign --force --sign - --timestamp=none "$APP"
echo "built $APP"

if [[ "$INSTALL" == 1 ]]; then
  DEST="$HOME/Applications/PetBar.app"
  mkdir -p "$HOME/Applications"
  pkill -x PetBar 2>/dev/null || true
  rm -rf "$DEST"
  cp -R "$APP" "$DEST"
  open "$DEST"
  echo "installed $DEST (use the menu's 登录时启动 to start it at login)"
fi
