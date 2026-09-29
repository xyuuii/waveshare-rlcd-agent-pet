#!/bin/bash
# Installs or repairs codex-pet-bridge on this Mac.
#
#   tools/mac/install.sh                 show what would change (nothing is touched)
#   tools/mac/install.sh --apply         make the changes
#   tools/mac/install.sh --rollback DIR  put back the files saved in DIR
#
# --apply:
#   1. copies this bridge to ~/.codex-pet-bridge/app, a stable path outside
#      iCloud Drive and ~/Documents that launchd can always reach
#   2. keeps the bridge token in ~/.codex-pet-bridge/token (0600), taken from
#      the existing LaunchAgent, so the board keeps working without a reflash
#   3. keeps runtime state (event log, display settings, animations) in
#      ~/.codex-pet-bridge/state, importing the old event log once
#   4. rewrites the bridge and agent-sync LaunchAgents to run from there
#      (the token is no longer written into the plist) and restarts them
#   5. points the Hermes hooks and the OpenClaw plugin at the new copy
#   6. checks /health and /status
# Every file it changes is copied to ~/.codex-pet-bridge/backup/<time>/ first.
# The token is never printed.
#
# Options: --skip-launchd --skip-hermes --skip-openclaw --node /path/to/node
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BASE="${PET_HOME:-$HOME/.codex-pet-bridge}"
APP="$BASE/app"
STATE="$BASE/state"
TOKEN_FILE="$BASE/token"
LOG_DIR="$HOME/Library/Logs/codex-pet-bridge"
AGENTS_DIR="$HOME/Library/LaunchAgents"
BRIDGE_LABEL="net.vcxzvfe.codex-pet-bridge"
SYNC_LABEL="net.vcxzvfe.codex-pet-agent-sync"
BRIDGE_PLIST="$AGENTS_DIR/$BRIDGE_LABEL.plist"
SYNC_PLIST="$AGENTS_DIR/$SYNC_LABEL.plist"
HERMES_CONFIG="$HOME/.hermes/config.yaml"
HERMES_ALLOWLIST="$HOME/.hermes/shell-hooks-allowlist.json"
OPENCLAW_LINK="$HOME/.openclaw/extensions/openclaw-pet-bridge"
OPENCLAW_CONFIG="$HOME/.openclaw/openclaw.json"
LAUNCHCTL="${LAUNCHCTL:-/bin/launchctl}"
PLUTIL="${PLUTIL:-/usr/bin/plutil}"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$BASE/backup/$STAMP"
UID_NUM="$(id -u)"

APPLY=0
DO_LAUNCHD=1
DO_HERMES=1
DO_OPENCLAW=1
NODE="${PET_NODE:-}"
ROLLBACK=""

usage() {
  sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=1 ;;
    --skip-launchd) DO_LAUNCHD=0 ;;
    --skip-hermes) DO_HERMES=0 ;;
    --skip-openclaw) DO_OPENCLAW=0 ;;
    --node) NODE="${2:-}"; shift ;;
    --rollback) ROLLBACK="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
item() { printf '  - %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Runs a command with --apply, otherwise only shows it.
act() {
  if [[ "$APPLY" == 1 ]]; then
    "$@"
  else
    printf '    (dry run) %s\n' "$*"
  fi
}

plist_get() {
  [[ -f "$1" ]] || return 0
  "$PLUTIL" -extract "$2" raw -o - "$1" 2>/dev/null || true
}

backup() {
  local path="$1"
  [[ -e "$path" || -L "$path" ]] || return 0
  mkdir -p "$BACKUP"
  chmod 700 "$BASE/backup" "$BACKUP" 2>/dev/null || true
  if [[ -L "$path" ]]; then
    readlink "$path" > "$BACKUP/$(basename "$path").link"
  else
    cp -p "$path" "$BACKUP/$(basename "$path")"
  fi
}

xml() {
  printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'
}

# Rewrites every path that ends in <old-suffix> inside a file to <new-path>.
# Portable (BSD and GNU sed), keeps the file's inode and permissions.
retarget() {
  local file="$1" pattern="$2" replacement="$3" tmp
  tmp="$(mktemp "${TMPDIR:-/tmp}/pet-install.XXXXXX")"
  sed -E "s#[^[:space:]\"'=]*${pattern}#${replacement}#g" "$file" > "$tmp"
  cat "$tmp" > "$file"
  rm -f "$tmp"
}

bootstrap_agent() {
  local label="$1" plist="$2" attempt
  "$LAUNCHCTL" bootout "gui/$UID_NUM/$label" 2>/dev/null || true
  for attempt in 1 2 3 4 5; do
    if "$LAUNCHCTL" bootstrap "gui/$UID_NUM" "$plist" 2>/dev/null; then
      "$LAUNCHCTL" enable "gui/$UID_NUM/$label" 2>/dev/null || true
      return 0
    fi
    sleep 1
  done
  die "launchctl bootstrap failed for $label (see: launchctl print gui/$UID_NUM/$label)"
}

# ---------------------------------------------------------------- rollback

if [[ -n "$ROLLBACK" ]]; then
  [[ -d "$ROLLBACK" ]] || die "no backup at $ROLLBACK"
  say "Restoring from $ROLLBACK"
  for label in "$BRIDGE_LABEL" "$SYNC_LABEL"; do
    if [[ -f "$ROLLBACK/$label.plist" ]]; then
      item "LaunchAgent $label"
      cp -p "$ROLLBACK/$label.plist" "$AGENTS_DIR/$label.plist"
      bootstrap_agent "$label" "$AGENTS_DIR/$label.plist"
    fi
  done
  for file in "$HERMES_CONFIG" "$HERMES_ALLOWLIST" "$OPENCLAW_CONFIG"; do
    if [[ -f "$ROLLBACK/$(basename "$file")" ]]; then
      item "$file"
      cat "$ROLLBACK/$(basename "$file")" > "$file"
    fi
  done
  if [[ -f "$ROLLBACK/$(basename "$OPENCLAW_LINK").link" ]]; then
    item "$OPENCLAW_LINK"
    ln -sfn "$(cat "$ROLLBACK/$(basename "$OPENCLAW_LINK").link")" "$OPENCLAW_LINK"
  fi
  say "Done. ~/.codex-pet-bridge/app, state and token were left in place."
  exit 0
fi

# ---------------------------------------------------------------- checks

[[ "$(uname -s)" == "Darwin" || -n "${PET_INSTALL_TEST:-}" ]] || die "this installer is for macOS"
[[ -f "$SRC/src/bridge-server.js" ]] || die "run this from the codex-pet-bridge checkout ($SRC)"

if [[ -z "$NODE" ]]; then NODE="$(plist_get "$BRIDGE_PLIST" ProgramArguments.0)"; fi
if [[ -z "$NODE" || ! -x "$NODE" ]]; then NODE="$(command -v node || true)"; fi
[[ -n "$NODE" && -x "$NODE" ]] || die "node not found; pass --node /path/to/node"
NODE_MAJOR="$("$NODE" -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0)"
[[ "$NODE_MAJOR" -ge 20 ]] || die "node $NODE is too old (need 20 or newer)"

OLD_HOST="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_HOST)"
OLD_PORT="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_PORT)"
OLD_PREFIX="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_AGENT_SYNC_PREFIX)"
HOST_BIND="${PET_BRIDGE_HOST:-${OLD_HOST:-0.0.0.0}}"
PORT="${PET_BRIDGE_PORT:-${OLD_PORT:-17366}}"
PREFIX="${PET_AGENT_SYNC_PREFIX:-${OLD_PREFIX:-mac}}"

# Where the old LaunchAgent kept its event log. ~/Documents may have moved
# into iCloud Drive, which is exactly what broke the old setup.
OLD_WORKDIR="$(plist_get "$BRIDGE_PLIST" WorkingDirectory)"
OLD_STATE_DIR=""
if [[ -n "$OLD_WORKDIR" ]]; then
  if [[ -d "$OLD_WORKDIR" ]]; then
    OLD_STATE_DIR="$OLD_WORKDIR"
  else
    case "$OLD_WORKDIR" in
      "$HOME/Documents/"*)
        candidate="$HOME/Library/Mobile Documents/com~apple~CloudDocs/Documents/${OLD_WORKDIR#"$HOME/Documents/"}"
        [[ -d "$candidate" ]] && OLD_STATE_DIR="$candidate"
        ;;
    esac
  fi
fi

if [[ "$APPLY" == 1 ]]; then
  say "Installing codex-pet-bridge (backups: $BACKUP)"
else
  say "Dry run: nothing is changed. Re-run with --apply to do this:"
fi
say "  source: $SRC"
say "  node:   $NODE (v$NODE_MAJOR)"

# ---------------------------------------------------------------- 1. app copy

step "1. Bridge code -> $APP"
if [[ "$SRC" == "$APP" ]]; then
  item "already running from $APP; nothing to copy"
else
  item "copy src/, ui/, integrations/ and package files (tests, tools and docs stay behind)"
  act mkdir -p "$APP"
  act rsync -a --delete \
    --exclude '.git' --exclude 'node_modules' --exclude 'test/' --exclude 'tools/' \
    --exclude 'docs/' --exclude 'macos/' --exclude '*.jsonl' --exclude 'bridge-state.json' \
    --exclude '.DS_Store' "$SRC/" "$APP/"
fi

# ---------------------------------------------------------------- 2. token

step "2. Token -> $TOKEN_FILE"
TOKEN=""
if [[ -s "$TOKEN_FILE" ]]; then
  item "keep the existing token file"
else
  TOKEN="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_TOKEN)"
  origin="the old bridge LaunchAgent"
  if [[ -z "$TOKEN" ]]; then
    TOKEN="$(plist_get "$SYNC_PLIST" EnvironmentVariables.PET_BRIDGE_URL | sed -n 's/.*[?&]token=\([^&]*\).*/\1/p')"
    origin="the old agent-sync LaunchAgent"
  fi
  if [[ -z "$TOKEN" ]]; then
    TOKEN="$(openssl rand -hex 24)"
    origin="a new random value: the board must be reflashed with it"
  fi
  item "write it (0600), taken from $origin; the value is not shown"
  if [[ "$APPLY" == 1 ]]; then
    mkdir -p "$BASE"
    chmod 700 "$BASE"
    (umask 077 && printf '%s\n' "$TOKEN" > "$TOKEN_FILE.tmp" && mv "$TOKEN_FILE.tmp" "$TOKEN_FILE")
    chmod 600 "$TOKEN_FILE"
  fi
fi
TOKEN=""

# ---------------------------------------------------------------- 3. state

step "3. Runtime state -> $STATE"
act mkdir -p "$STATE/anim" "$LOG_DIR"
if [[ -f "$STATE/events.jsonl" ]]; then
  item "event log already there"
elif [[ -n "$OLD_STATE_DIR" && -f "$OLD_STATE_DIR/events.jsonl" ]]; then
  item "import the old event log and bridge state from $OLD_STATE_DIR"
  act cp -p "$OLD_STATE_DIR/events.jsonl" "$STATE/events.jsonl"
  if [[ -f "$OLD_STATE_DIR/bridge-state.json" ]]; then
    act cp -p "$OLD_STATE_DIR/bridge-state.json" "$STATE/bridge-state.json"
  fi
else
  item "start with an empty event log"
fi

# ---------------------------------------------------------------- 4. launchd

write_bridge_plist() {
  cat > "$1" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$BRIDGE_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$(xml "$NODE")</string>
    <string>$(xml "$APP/src/bridge-server.js")</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$(xml "$STATE")</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>/usr/bin:/bin:/usr/sbin:/sbin</string>
    <key>PET_BRIDGE_HOST</key>
    <string>$(xml "$HOST_BIND")</string>
    <key>PET_BRIDGE_PORT</key>
    <string>$(xml "$PORT")</string>
    <key>PET_BRIDGE_TOKEN_FILE</key>
    <string>$(xml "$TOKEN_FILE")</string>
    <key>PET_BRIDGE_LOG</key>
    <string>$(xml "$STATE/events.jsonl")</string>
    <key>PET_BRIDGE_STATE</key>
    <string>$(xml "$STATE/bridge-state.json")</string>
    <key>PET_AGENT_SYNC_PREFIX</key>
    <string>$(xml "$PREFIX")</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>10</integer>
  <key>StandardOutPath</key>
  <string>$(xml "$LOG_DIR/bridge.stdout.log")</string>
  <key>StandardErrorPath</key>
  <string>$(xml "$LOG_DIR/bridge.stderr.log")</string>
</dict>
</plist>
PLIST
}

write_sync_plist() {
  cat > "$1" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$SYNC_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$(xml "$NODE")</string>
    <string>$(xml "$APP/src/agent-sync.js")</string>
    <string>--watch</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$(xml "$STATE")</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>/usr/bin:/bin:/usr/sbin:/sbin</string>
    <key>PET_AGENT_SYNC_PREFIX</key>
    <string>$(xml "$PREFIX")</string>
    <key>PET_BRIDGE_URL</key>
    <string>$(xml "http://127.0.0.1:$PORT/events")</string>
    <key>PET_BRIDGE_TOKEN_FILE</key>
    <string>$(xml "$TOKEN_FILE")</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>10</integer>
  <key>StandardOutPath</key>
  <string>$(xml "$LOG_DIR/agent-sync.stdout.log")</string>
  <key>StandardErrorPath</key>
  <string>$(xml "$LOG_DIR/agent-sync.stderr.log")</string>
</dict>
</plist>
PLIST
}

step "4. LaunchAgents"
if [[ "$DO_LAUNCHD" == 0 ]]; then
  item "skipped (--skip-launchd)"
else
  item "$BRIDGE_LABEL: node $APP/src/bridge-server.js on $HOST_BIND:$PORT, token from the file"
  item "$SYNC_LABEL: node $APP/src/agent-sync.js --watch (prefix $PREFIX)"
  item "logs: $LOG_DIR"
  if [[ "$APPLY" == 1 ]]; then
    mkdir -p "$AGENTS_DIR"
    backup "$BRIDGE_PLIST"
    backup "$SYNC_PLIST"
    write_bridge_plist "$BRIDGE_PLIST.new"
    write_sync_plist "$SYNC_PLIST.new"
    "$PLUTIL" -lint "$BRIDGE_PLIST.new" >/dev/null
    "$PLUTIL" -lint "$SYNC_PLIST.new" >/dev/null
    mv "$BRIDGE_PLIST.new" "$BRIDGE_PLIST"
    mv "$SYNC_PLIST.new" "$SYNC_PLIST"
    bootstrap_agent "$BRIDGE_LABEL" "$BRIDGE_PLIST"
    bootstrap_agent "$SYNC_LABEL" "$SYNC_PLIST"
    item "restarted both"
  else
    printf '    (dry run) rewrite %s and %s, then launchctl bootout/bootstrap both\n' "$BRIDGE_PLIST" "$SYNC_PLIST"
  fi
fi

# ---------------------------------------------------------------- 5. hooks

step "5. Hermes hooks and OpenClaw plugin"
HERMES_PATTERN='codex-pet-bridge/src/hermes-hook\.js'
if [[ "$DO_HERMES" == 0 ]]; then
  item "Hermes: skipped (--skip-hermes)"
else
  for file in "$HERMES_CONFIG" "$HERMES_ALLOWLIST"; do
    if [[ -f "$file" ]] && grep -Eq "[^[:space:]\"'=]*$HERMES_PATTERN" "$file"; then
      count="$(grep -Ec "[^[:space:]\"'=]*$HERMES_PATTERN" "$file" || true)"
      item "$file: $count line(s) -> $APP/src/hermes-hook.js"
      if [[ "$APPLY" == 1 ]]; then
        backup "$file"
        retarget "$file" "$HERMES_PATTERN" "$APP/src/hermes-hook.js"
      fi
    elif [[ -f "$file" ]]; then
      item "$file: nothing to change"
    fi
  done
fi

if [[ "$DO_OPENCLAW" == 0 ]]; then
  item "OpenClaw: skipped (--skip-openclaw)"
else
  PLUGIN="$APP/integrations/openclaw-pet-bridge"
  if [[ -L "$OPENCLAW_LINK" ]]; then
    if [[ "$(readlink "$OPENCLAW_LINK")" == "$PLUGIN" ]]; then
      item "OpenClaw plugin link already points at $PLUGIN"
    else
      item "OpenClaw plugin link -> $PLUGIN"
      if [[ "$APPLY" == 1 ]]; then
        backup "$OPENCLAW_LINK"
        ln -sfn "$PLUGIN" "$OPENCLAW_LINK"
      fi
    fi
  elif [[ -e "$OPENCLAW_LINK" ]]; then
    item "OpenClaw: $OPENCLAW_LINK is not a link; leaving it alone"
  else
    item "OpenClaw plugin not installed; nothing to do"
  fi
  OPENCLAW_PATTERN='codex-pet-bridge/integrations/openclaw-pet-bridge'
  if [[ -f "$OPENCLAW_CONFIG" ]] && grep -Eq "[^[:space:]\"'=]*$OPENCLAW_PATTERN" "$OPENCLAW_CONFIG" \
      && grep -E "[^[:space:]\"'=]*$OPENCLAW_PATTERN" "$OPENCLAW_CONFIG" | grep -vq "$PLUGIN"; then
    item "$OPENCLAW_CONFIG: plugin path -> $PLUGIN"
    if [[ "$APPLY" == 1 ]]; then
      backup "$OPENCLAW_CONFIG"
      retarget "$OPENCLAW_CONFIG" "$OPENCLAW_PATTERN" "$PLUGIN"
    fi
  fi
fi

# ---------------------------------------------------------------- 6. check

step "6. Check"
if [[ "$APPLY" == 0 ]]; then
  item "(dry run) would wait for http://127.0.0.1:$PORT/health and read /status with the token"
  say ""
  say "Run again with --apply to make these changes."
  exit 0
fi

healthy=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  # Any HTTP answer means it is up (with a token, /health also wants the token).
  code="$(curl -s -m 2 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/health" || true)"
  if [[ -n "$code" && "$code" != 000 ]]; then healthy=1; break; fi
  sleep 1
done
if [[ "$healthy" == 1 ]]; then
  item "bridge answers on :$PORT"
else
  item "bridge does not answer yet; look at $LOG_DIR/bridge.stderr.log"
fi

header="$(mktemp "${TMPDIR:-/tmp}/pet-install.XXXXXX")"
chmod 600 "$header"
printf 'Authorization: Bearer %s\n' "$(cat "$TOKEN_FILE")" > "$header"
if curl -s -m 3 -H @"$header" "http://127.0.0.1:$PORT/status" | grep -q '"ok": true'; then
  item "/status works with the token file"
else
  item "/status did not answer with the token file"
fi
rm -f "$header"

say ""
say "Done."
say "  Dashboard: http://127.0.0.1:$PORT/ui/  (PetBar's 打开控制台 fills in the token)"
say "  Backups:   $BACKUP"
say "  Undo:      $0 --rollback \"$BACKUP\""
