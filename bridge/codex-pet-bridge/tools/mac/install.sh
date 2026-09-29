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
# Every file it changes is copied to ~/.codex-pet-bridge/backup/<time>/ first;
# the very first run also keeps a copy in ~/.codex-pet-bridge/backup/original/.
# Other environment variables of the old LaunchAgents are carried over.
# The token is never printed.
#
# Options: --skip-launchd --skip-hermes --skip-openclaw --node /path/to/node
set -Eeuo pipefail

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
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
BACKUP="$BASE/backup/$STAMP"
ORIGINAL="$BASE/backup/original"
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

rollback_hint() {
  if [[ "$APPLY" == 1 && -d "$BACKUP" ]]; then
    printf 'Files changed so far were saved first. To put them back:\n  %s --rollback "%s"\n' "$0" "$BACKUP" >&2
  fi
}
die() {
  printf 'error: %s\n' "$*" >&2
  rollback_hint
  exit 1
}
trap 'status=$?; if [[ $status -ne 0 ]]; then printf "error: install stopped (line %s)\n" "$LINENO" >&2; rollback_hint; fi' ERR

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

# Saves a file's content (following symlinks, since edits go through them) in
# this run's backup and, on the very first run, in backup/original.
backup() {
  local path="$1" dir
  [[ -e "$path" ]] || return 0
  for dir in "$BACKUP" "$ORIGINAL"; do
    [[ "$dir" == "$ORIGINAL" && -e "$ORIGINAL/$(basename "$path")" ]] && continue
    mkdir -p "$dir"
    chmod 700 "$BASE/backup" "$dir" 2>/dev/null || true
    cp -pL "$path" "$dir/$(basename "$path")"
  done
}

# Saves where a symlink points (used for the OpenClaw plugin link).
backup_link() {
  local path="$1" dir
  [[ -L "$path" ]] || return 0
  for dir in "$BACKUP" "$ORIGINAL"; do
    [[ "$dir" == "$ORIGINAL" && -e "$ORIGINAL/$(basename "$path").link" ]] && continue
    mkdir -p "$dir"
    chmod 700 "$BASE/backup" "$dir" 2>/dev/null || true
    readlink "$path" > "$dir/$(basename "$path").link"
  done
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
  # A label disabled earlier (launchctl unload -w) refuses to bootstrap.
  "$LAUNCHCTL" enable "gui/$UID_NUM/$label" 2>/dev/null || true
  for attempt in 1 2 3 4 5; do
    if "$LAUNCHCTL" bootstrap "gui/$UID_NUM" "$plist" 2>/dev/null; then
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

node_major() {
  local major
  major="$("$1" -p 'process.versions.node.split(".")[0]' 2>/dev/null || true)"
  case "$major" in
    ''|*[!0-9]*) echo 0 ;;
    *) echo "$major" ;;
  esac
}
NODE_MAJOR=0
for candidate in "$NODE" "$(plist_get "$BRIDGE_PLIST" ProgramArguments.0)" "$(command -v node || true)"; do
  [[ -n "$candidate" && -x "$candidate" ]] || continue
  NODE_MAJOR="$(node_major "$candidate")"
  if [[ "$NODE_MAJOR" -ge 20 ]]; then
    NODE="$candidate"
    break
  fi
done
[[ "$NODE_MAJOR" -ge 20 ]] || die "no node 20 or newer found; pass --node /path/to/node"

OLD_HOST="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_HOST)"
OLD_PORT="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_PORT)"
OLD_PREFIX="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_AGENT_SYNC_PREFIX)"
# The board polls over the LAN, so a fresh install listens on 0.0.0.0 (with the token).
HOST_BIND="${PET_BRIDGE_HOST:-${OLD_HOST:-0.0.0.0}}"
PORT="${PET_BRIDGE_PORT:-${OLD_PORT:-17366}}"
PREFIX="${PET_AGENT_SYNC_PREFIX:-${OLD_PREFIX:-mac}}"
origin_of() {  # env-name old-value
  if [[ -n "${!1:-}" ]]; then echo "\$$1"; elif [[ -n "$2" ]]; then echo "old LaunchAgent"; else echo "default"; fi
}

# Environment variables of an old plist that this script does not manage
# (webhooks, XiaoZhi settings, ...), as plist XML. Values are never printed.
extra_env_xml() {  # plist, managed keys...
  local plist="$1" key value
  shift
  [[ -f "$plist" ]] || return 0
  "$PLUTIL" -convert json -o - "$plist" 2>/dev/null \
    | "$NODE" -e '
        const data = JSON.parse(require("fs").readFileSync(0, "utf8"));
        const skip = new Set(process.argv.slice(1));
        for (const [key, value] of Object.entries(data.EnvironmentVariables || {})) {
          if (skip.has(key) || typeof value !== "string" || /[\t\n]/.test(value)) continue;
          process.stdout.write(`${key}\t${value}\n`);
        }' "$@" \
    | while IFS=$'\t' read -r key value; do
        printf '    <key>%s</key>\n    <string>%s</string>\n' "$(xml "$key")" "$(xml "$value")"
      done || true
}
extra_env_names() {
  printf '%s' "$1" | sed -n 's#.*<key>\(.*\)</key>.*#\1#p' | paste -sd ' ' -
}
BRIDGE_EXTRA="$(extra_env_xml "$BRIDGE_PLIST" PATH PET_BRIDGE_HOST PET_BRIDGE_PORT PET_BRIDGE_TOKEN PET_BRIDGE_TOKEN_FILE \
  PET_BRIDGE_LOG PET_BRIDGE_STATE PET_AGENT_SYNC_PREFIX)"
SYNC_EXTRA="$(extra_env_xml "$SYNC_PLIST" PATH PET_AGENT_SYNC_PREFIX PET_BRIDGE_URL PET_BRIDGE_TOKEN PET_BRIDGE_TOKEN_FILE)"

# Where the old LaunchAgent kept its event log. ~/Documents may have moved
# into iCloud Drive, which is exactly what broke the old setup.
relocated() {  # a path under ~/Documents that may now live in iCloud Drive
  local path="$1" candidate
  if [[ -e "$path" ]]; then
    printf '%s' "$path"
    return 0
  fi
  case "$path" in
    "$HOME/Documents/"*)
      candidate="$HOME/Library/Mobile Documents/com~apple~CloudDocs/Documents/${path#"$HOME/Documents/"}"
      [[ -e "$candidate" ]] && printf '%s' "$candidate"
      ;;
  esac
  return 0
}
OLD_WORKDIR="$(plist_get "$BRIDGE_PLIST" WorkingDirectory)"
OLD_LOG="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_LOG)"
OLD_STATE_FILE="$(plist_get "$BRIDGE_PLIST" EnvironmentVariables.PET_BRIDGE_STATE)"
OLD_STATE_DIR=""
[[ -n "$OLD_WORKDIR" ]] && OLD_STATE_DIR="$(relocated "$OLD_WORKDIR")"
if [[ -z "$OLD_LOG" && -n "$OLD_STATE_DIR" ]]; then OLD_LOG="$OLD_STATE_DIR/events.jsonl"; fi
if [[ -z "$OLD_STATE_FILE" && -n "$OLD_STATE_DIR" ]]; then OLD_STATE_FILE="$OLD_STATE_DIR/bridge-state.json"; fi
[[ -n "$OLD_LOG" ]] && OLD_LOG="$(relocated "$OLD_LOG")"
[[ -n "$OLD_STATE_FILE" ]] && OLD_STATE_FILE="$(relocated "$OLD_STATE_FILE")"

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
elif [[ -n "$OLD_LOG" && -f "$OLD_LOG" ]]; then
  item "import the old event log and bridge state from $(dirname "$OLD_LOG")"
  act cp -p "$OLD_LOG" "$STATE/events.jsonl"
  if [[ -n "$OLD_STATE_FILE" && -f "$OLD_STATE_FILE" ]]; then
    act cp -p "$OLD_STATE_FILE" "$STATE/bridge-state.json"
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
$BRIDGE_EXTRA
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
$SYNC_EXTRA
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
  item "$BRIDGE_LABEL: node $APP/src/bridge-server.js, listening on $HOST_BIND:$PORT, token from the file"
  item "$SYNC_LABEL: node $APP/src/agent-sync.js --watch, source prefix \"$PREFIX\""
  item "host from $(origin_of PET_BRIDGE_HOST "$OLD_HOST"), port from $(origin_of PET_BRIDGE_PORT "$OLD_PORT"), prefix from $(origin_of PET_AGENT_SYNC_PREFIX "$OLD_PREFIX")"
  [[ -n "$BRIDGE_EXTRA" ]] && item "bridge keeps its other settings: $(extra_env_names "$BRIDGE_EXTRA")"
  [[ -n "$SYNC_EXTRA" ]] && item "agent-sync keeps its other settings: $(extra_env_names "$SYNC_EXTRA")"
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
        backup_link "$OPENCLAW_LINK"
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
say "  Undo this run:            $0 --rollback \"$BACKUP\""
say "  Back to the original:     $0 --rollback \"$ORIGINAL\""
