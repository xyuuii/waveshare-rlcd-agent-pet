#!/bin/bash
# Exercises tools/mac/install.sh against a fake home directory: an old broken
# setup (LaunchAgents pointing into ~/Documents, which moved to iCloud Drive),
# Hermes hooks and an OpenClaw plugin link. launchctl, plutil and rsync are
# replaced by small stubs; the stub launchctl really starts the bridge so the
# installer's health check runs against it. Nothing outside the temp dir is
# touched, so this is safe to run on the Mac as well as on Linux.
#
#   tools/mac/test-install.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/pet-install-test.XXXXXX")"
FAKE_HOME="$ROOT/home"
STUBS="$ROOT/stubs"
PORT=$((17950 + $$ % 40))
NODE_BIN="$(command -v node)"
OLD="$FAKE_HOME/Documents/Codex/2026-06-07/rlcd-https-docs-waveshare-net-esp32/work/codex-pet-bridge"
ICLOUD="$FAKE_HOME/Library/Mobile Documents/com~apple~CloudDocs/Documents/Codex/2026-06-07/rlcd-https-docs-waveshare-net-esp32/work/codex-pet-bridge"
TOKEN="tok-$(date +%s)-old"
FAILED=0

cleanup() {
  if [[ -f "$ROOT/pids" ]]; then
    while read -r pid; do kill "$pid" 2>/dev/null || true; done < "$ROOT/pids"
  fi
  rm -rf "$ROOT"
}
trap cleanup EXIT
KEEP_OUTPUT="${KEEP_OUTPUT:-}"

ok() { printf '  ok   %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*"; FAILED=$((FAILED + 1)); }
check() { if eval "$1"; then ok "$2"; else bad "$2"; fi; }

mkdir -p "$STUBS" "$FAKE_HOME/Library/LaunchAgents" "$ICLOUD" "$FAKE_HOME/.hermes" "$FAKE_HOME/.openclaw/extensions"

# --- stubs -------------------------------------------------------------------
cat > "$STUBS/plutil" <<'PY'
#!/usr/bin/env python3
import plistlib, sys
args = sys.argv[1:]
if args[0] == "-lint":
    plistlib.load(open(args[1], "rb"))
    print(f"{args[1]}: OK")
    sys.exit(0)
if args[0] == "-convert" and args[1] == "json":
    import json
    sys.stdout.write(json.dumps(plistlib.load(open(args[-1], "rb"))))
    sys.exit(0)
if args[0] == "-extract":
    keypath, path = args[1], args[-1]
    value = plistlib.load(open(path, "rb"))
    for part in keypath.split("."):
        value = value[int(part)] if isinstance(value, list) else value[part]
    sys.stdout.write(str(value))
    sys.exit(0)
sys.exit(1)
PY
cat > "$STUBS/launchctl" <<PY
#!/usr/bin/env python3
import os, plistlib, subprocess, sys
root = "$ROOT"
open(os.path.join(root, "launchctl.log"), "a").write(" ".join(sys.argv[1:]) + "\n")
if sys.argv[1] == "bootout":
    label = sys.argv[2].split("/")[-1]
    pidfile = os.path.join(root, label + ".pid")
    if os.path.exists(pidfile):
        try:
            os.kill(int(open(pidfile).read()), 15)
        except OSError:
            pass
        os.remove(pidfile)
if sys.argv[1] == "bootstrap":
    spec = plistlib.load(open(sys.argv[3], "rb"))
    env = dict(os.environ)
    env.update(spec.get("EnvironmentVariables", {}))
    env["PATH"] = os.environ["PATH"]
    out = open(os.path.join(root, spec["Label"] + ".out"), "a")
    try:
        proc = subprocess.Popen(spec["ProgramArguments"], cwd=spec.get("WorkingDirectory"), env=env,
                                stdout=out, stderr=out, start_new_session=True)
    except OSError as error:
        # launchd accepts the job and only fails when it spawns it (EX_CONFIG)
        out.write(f"spawn failed: {error}\n")
    else:
        open(os.path.join(root, spec["Label"] + ".pid"), "w").write(str(proc.pid))
        open(os.path.join(root, "pids"), "a").write(f"{proc.pid}\n")
PY
cat > "$STUBS/rsync" <<'PY'
#!/usr/bin/env python3
import fnmatch, os, shutil, sys
args = sys.argv[1:]
excludes, paths = [], []
i = 0
while i < len(args):
    if args[i] == "--exclude":
        excludes.append(args[i + 1]); i += 2; continue
    if not args[i].startswith("-"):
        paths.append(args[i])
    i += 1
src, dst = paths[-2].rstrip("/"), paths[-1].rstrip("/")
def ignore(directory, names):
    skipped = []
    for name in names:
        is_dir = os.path.isdir(os.path.join(directory, name))
        for pattern in excludes:
            if pattern.endswith("/"):
                if is_dir and fnmatch.fnmatch(name, pattern[:-1]): skipped.append(name)
            elif fnmatch.fnmatch(name, pattern): skipped.append(name)
    return skipped
if "--delete" in args and os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst, ignore=ignore, symlinks=True, dirs_exist_ok=True)
PY
chmod +x "$STUBS"/*
if ! command -v sqlite3 >/dev/null 2>&1; then
  printf '#!/bin/sh\nexit 1\n' > "$STUBS/sqlite3"
  chmod +x "$STUBS/sqlite3"
fi

# --- the broken old setup ------------------------------------------------------
cat > "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>EnvironmentVariables</key><dict>
    <key>PET_AGENT_SYNC_PREFIX</key><string>mac</string>
    <key>PET_BRIDGE_HOST</key><string>127.0.0.1</string>
    <key>PET_BRIDGE_PORT</key><string>$PORT</string>
    <key>PET_BRIDGE_TOKEN</key><string>$TOKEN</string>
    <key>XIAOZHI_HUB_URL</key><string>http://hub.local:8080/assistant/notifications?key=secret-hub-key</string>
  </dict>
  <key>KeepAlive</key><true/>
  <key>Label</key><string>net.vcxzvfe.codex-pet-bridge</string>
  <key>ProgramArguments</key><array><string>$NODE_BIN</string><string>$OLD/src/bridge-server.js</string></array>
  <key>RunAtLoad</key><true/>
  <key>WorkingDirectory</key><string>$OLD</string>
</dict></plist>
PLIST
cat > "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-agent-sync.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>EnvironmentVariables</key><dict>
    <key>PET_AGENT_SYNC_PREFIX</key><string>mac</string>
    <key>PET_BRIDGE_URL</key><string>http://127.0.0.1:$PORT/events?token=$TOKEN</string>
  </dict>
  <key>Label</key><string>net.vcxzvfe.codex-pet-agent-sync</string>
  <key>ProgramArguments</key><array><string>$NODE_BIN</string><string>$OLD/src/agent-sync.js</string><string>--watch</string></array>
  <key>WorkingDirectory</key><string>$OLD</string>
</dict></plist>
PLIST
printf '{"id":"e1","time":"2026-08-16T09:00:00Z","source":"mac-codex","status":"completed","type":"status","message":"old"}\n' > "$ICLOUD/events.jsonl"
printf '{}\n' > "$ICLOUD/bridge-state.json"
cat > "$FAKE_HOME/.hermes/config.yaml" <<YAML
model: something
hooks:
  pre_tool_call:
    - command: node $OLD/src/hermes-hook.js
  post_llm_call:
    - command: node $OLD/src/hermes-hook.js
hooks_auto_accept: true
YAML
printf '{"allowed": ["node %s/src/hermes-hook.js"]}\n' "$OLD" > "$FAKE_HOME/.hermes/shell-hooks-allowlist.json"
ln -s "$OLD/integrations/openclaw-pet-bridge" "$FAKE_HOME/.openclaw/extensions/openclaw-pet-bridge"

run_install() {
  HOME="$FAKE_HOME" PATH="$STUBS:$PATH" PET_INSTALL_TEST=1 LAUNCHCTL="$STUBS/launchctl" PLUTIL="$STUBS/plutil" \
    "$SRC/tools/mac/install.sh" --node "$NODE_BIN" "$@"
}

snapshot() {  # names, sizes, times and modes of everything under the fake home (GNU or BSD stat)
  (cd "$FAKE_HOME" && find . -print | LC_ALL=C sort | while IFS= read -r path; do
    stat -c '%n %s %Y %a' "$path" 2>/dev/null || stat -f '%N %z %m %Lp' "$path"
  done) | cksum
}

echo "dry run"
before="$(snapshot)"
run_install > "$ROOT/dry.txt"
check '[[ "$(snapshot)" == "$before" ]]' "dry run changes nothing"
check 'grep -q "import the old event log" "$ROOT/dry.txt"' "dry run finds the old event log in iCloud Drive"
check 'grep -q "taken from the old bridge LaunchAgent" "$ROOT/dry.txt"' "dry run reuses the old token"
check '! grep -q "$TOKEN" "$ROOT/dry.txt"' "dry run never prints the token"
check 'grep -q "keeps its other settings: XIAOZHI_HUB_URL" "$ROOT/dry.txt"' "dry run lists carried-over settings"
check '! grep -q "secret-hub-key" "$ROOT/dry.txt"' "dry run never prints their values"

echo "apply"
run_install --apply > "$ROOT/apply.txt" 2>&1 || { cat "$ROOT/apply.txt"; bad "install --apply exited non-zero"; }
check '! grep -q "$TOKEN" "$ROOT/apply.txt"' "apply never prints the token"
TOKEN_FILE="$FAKE_HOME/.codex-pet-bridge/token"
check '[[ "$(cat "$TOKEN_FILE")" == "$TOKEN" ]]' "token file keeps the board's token"
check '[[ "$(stat -c %a "$TOKEN_FILE" 2>/dev/null || stat -f %Lp "$TOKEN_FILE")" == 600 ]]' "token file is 0600"
check '! grep -q "$TOKEN" "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist"' "bridge plist no longer holds the token"
check '! grep -q "$TOKEN" "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-agent-sync.plist"' "agent-sync plist no longer holds the token"
check 'grep -q "$FAKE_HOME/.codex-pet-bridge/app/src/bridge-server.js" "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist"' "bridge plist runs the installed copy"
check 'grep -q "secret-hub-key" "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist"' "other settings carried over"
check '"$STUBS/plutil" -lint "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist" >/dev/null' "new bridge plist is valid"
check '[[ -f "$FAKE_HOME/.codex-pet-bridge/app/ui/index.html" && ! -d "$FAKE_HOME/.codex-pet-bridge/app/test" ]]' "app copy has the dashboard and no tests"
check 'grep -q "\"old\"" "$FAKE_HOME/.codex-pet-bridge/state/events.jsonl"' "old event log imported"
check '[[ "$(grep -c "$FAKE_HOME/.codex-pet-bridge/app/src/hermes-hook.js" "$FAKE_HOME/.hermes/config.yaml")" == 2 ]]' "Hermes hooks retargeted"
check '! grep -q "Documents/Codex" "$FAKE_HOME/.hermes/config.yaml"' "no old Hermes paths left"
check 'grep -q "\"node $FAKE_HOME/.codex-pet-bridge/app/src/hermes-hook.js\"" "$FAKE_HOME/.hermes/shell-hooks-allowlist.json"' "Hermes allowlist retargeted"
check '[[ "$(readlink "$FAKE_HOME/.openclaw/extensions/openclaw-pet-bridge")" == "$FAKE_HOME/.codex-pet-bridge/app/integrations/openclaw-pet-bridge" ]]' "OpenClaw link retargeted"
check 'grep -q "bridge answers" "$ROOT/apply.txt"' "bridge came up"
check 'grep -q "/status works with the token file" "$ROOT/apply.txt"' "status works with the token file"
BACKUP_DIR="$(ls -d "$FAKE_HOME/.codex-pet-bridge/backup/"2* | head -1)"
ORIGINAL_DIR="$FAKE_HOME/.codex-pet-bridge/backup/original"
check '[[ -f "$BACKUP_DIR/net.vcxzvfe.codex-pet-bridge.plist" && -f "$BACKUP_DIR/config.yaml" && -f "$BACKUP_DIR/openclaw-pet-bridge.link" ]]' "backups written"
check '[[ -f "$ORIGINAL_DIR/config.yaml" ]] && grep -q "Documents/Codex" "$ORIGINAL_DIR/config.yaml"' "original setup kept separately"

echo "apply again"
sleep 1
run_install --apply > "$ROOT/again.txt" 2>&1 || bad "second --apply exited non-zero"
check 'grep -q "keep the existing token file" "$ROOT/again.txt"' "token kept on re-run"
check 'grep -q "config.yaml: nothing to change" "$ROOT/again.txt"' "Hermes untouched on re-run"
check 'grep -q "already points" "$ROOT/again.txt"' "OpenClaw untouched on re-run"
check 'grep -q "/status works" "$ROOT/again.txt"' "bridge healthy after re-run"

check 'grep -q "Documents/Codex" "$ORIGINAL_DIR/config.yaml"' "original backup untouched by the second run"

echo "rollback"
run_install --rollback "$ORIGINAL_DIR" > "$ROOT/rollback.txt" 2>&1 || bad "rollback exited non-zero"
check 'grep -q "$TOKEN" "$FAKE_HOME/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist"' "old bridge plist restored"
check 'grep -q "Documents/Codex" "$FAKE_HOME/.hermes/config.yaml"' "old Hermes config restored"
check '[[ "$(readlink "$FAKE_HOME/.openclaw/extensions/openclaw-pet-bridge")" == "$OLD/integrations/openclaw-pet-bridge" ]]' "old OpenClaw link restored"

if [[ -n "$KEEP_OUTPUT" ]]; then
  for f in apply again rollback; do echo "--- $f"; cat "$ROOT/$f.txt"; done
fi
echo
if [[ "$FAILED" == 0 ]]; then echo "all installer checks passed"; else echo "$FAILED installer check(s) failed"; exit 1; fi
