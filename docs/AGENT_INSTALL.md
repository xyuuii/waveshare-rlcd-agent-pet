# Install guide for coding agents

This is a runbook for Codex, Claude Code or any other agent asked to install,
repair or upgrade this project on a Mac. People can follow it too. It covers
the bridge (LaunchAgents), the firmware on the Waveshare ESP32-S3-RLCD-4.2 and
the PetBar menu bar app, with a check after every step.

If you only need the overview, read the [README](../README.md) first.

## Ground rules

Follow these before and during every step:

1. **Ask before anything that changes the machine or the board.** Get the
   user's explicit go-ahead for each of: running `install.sh --apply`,
   flashing the board, building/installing PetBar, editing Hermes, OpenClaw,
   Claude or Codex configuration, and pushing to GitHub. Read-only checks
   need no permission.
2. **Never print, log, paste or commit secrets.** That means the Wi-Fi
   password, the bridge token (`~/.codex-pet-bridge/token`, the `token=` part
   of `kBridgeUrl`) and `include/secrets.h`. Use the token through files
   (`curl -H @file`), never on a command line you echo back or in a URL you
   show. `include/secrets.h` is git-ignored; check `git status` before any
   commit.
3. **Treat runtime logs as private.** `~/.codex-pet-bridge/state/events.jsonl`
   and the files in `~/Library/Logs/codex-pet-bridge/` contain project paths
   and session details. Read them only to diagnose, quote only what you need,
   never commit them.
4. **Keep the bridge protected.** It listens on `0.0.0.0:17366` so the board
   can reach it, which is only acceptable with the token. Do not remove the
   token or add `PET_BRIDGE_ALLOW_UNAUTH_REMOTE=1`.
5. **Confirm the serial port before flashing.** The board is the port whose
   hardware ID contains `303A:1001` (ESP32-S3 USB Serial/JTAG). Never flash
   anything else.
6. **Do not trust earlier results.** Past test, flash or deploy results say
   nothing about the current state. Check again.

## What gets installed where

| Piece | Location |
| --- | --- |
| Bridge code (copied by the installer) | `~/.codex-pet-bridge/app/` |
| Bridge token (0600) | `~/.codex-pet-bridge/token` |
| Event log, bridge state, display settings, animations | `~/.codex-pet-bridge/state/` |
| Installer backups (`original/` = before the first run) | `~/.codex-pet-bridge/backup/` |
| LaunchAgents | `~/Library/LaunchAgents/net.vcxzvfe.codex-pet-bridge.plist`, `net.vcxzvfe.codex-pet-agent-sync.plist` |
| Logs | `~/Library/Logs/codex-pet-bridge/{bridge,agent-sync}.{stdout,stderr}.log` |
| Dashboard | `http://127.0.0.1:17366/ui/` (API needs the token) |
| Menu bar app | `~/Applications/PetBar.app` (bundle id `net.vcxzvfe.petbar`) |
| Board settings (Wi-Fi, bridge URL) | `firmware/agent_pet_display/include/secrets.h` (git-ignored) |

The board polls `GET /esp32/poll` every 3 seconds, so the Mac needs a fixed
LAN address (a DHCP reservation on the router). The address is compiled into
the firmware.

## Prerequisites

Check each and report what is missing instead of installing system software
on your own:

```bash
sw_vers -productVersion                 # macOS 13 or newer
node -v                                 # Node.js 20 or newer
git --version && xcrun --sdk macosx --find swiftc   # Xcode Command Line Tools
ipconfig getifaddr en0                  # the Mac's LAN address (Wi-Fi); en1 on some Macs
ls /dev/cu.usbmodem* 2>/dev/null        # the board, when it is plugged in over USB-C
```

PlatformIO Core 6.1 or newer is needed for the firmware. Use `pio` if it is
on `PATH`; otherwise use a virtual environment's Python:
`<venv>/bin/python -m platformio`. If a venv was moved to another folder, its
`bin/pio` script breaks (the shebang still points at the old path) but
`<venv>/bin/python -m platformio` keeps working.

## Step 0 — look at the current state (read-only)

```bash
for L in net.vcxzvfe.codex-pet-bridge net.vcxzvfe.codex-pet-agent-sync; do
  echo "== $L"
  launchctl print "gui/$(id -u)/$L" 2>&1 | grep -E '^[[:space:]]*(state|pid|last exit code|program) =|Could not find'
done
curl -s -m 3 -o /dev/null -w 'health %{http_code}\n' http://127.0.0.1:17366/health   # 200, or 401 with a token
ls -la ~/.codex-pet-bridge 2>/dev/null
```

- `last exit code = 78: EX_CONFIG` means the LaunchAgent points at a path that
  no longer exists (for example after the project moved into iCloud Drive).
  Step 1 fixes that.
- To read the bridge status without showing the token:

```bash
H="$(mktemp)"; chmod 600 "$H"
printf 'Authorization: Bearer %s\n' "$(cat ~/.codex-pet-bridge/token)" > "$H"
curl -s -m 5 -H @"$H" http://127.0.0.1:17366/status | node -e '
  let t = ""; process.stdin.on("data", d => t += d).on("end", () => {
    const s = JSON.parse(t), d = s.device || {};
    console.log("bridge", s.bridge.version, "board online", d.online, "firmware", d.firmware || "(old)",
      "battery", d.battery, "page", d.page, "clock", d.clockStyle, "time ok", d.timeValid);
    console.log("current", JSON.stringify(s.current));
  });'
rm -f "$H"
```

## Step 1 — bridge (LaunchAgents)

Run from the repository root. The first command changes nothing and prints
the plan; show it to the user and ask before running the second.

```bash
bridge/codex-pet-bridge/tools/mac/install.sh           # dry run
bridge/codex-pet-bridge/tools/mac/install.sh --apply   # after the user agrees
```

What `--apply` does:

- copies the bridge to `~/.codex-pet-bridge/app` (outside iCloud Drive and
  `~/Documents`, which launchd cannot always reach);
- keeps the token in `~/.codex-pet-bridge/token`, reusing the one in the old
  LaunchAgent so an already flashed board keeps working (a brand-new install
  gets a random token, and the board then needs `secrets.h` with it);
- imports the old event log once, rewrites both LaunchAgents (the token is no
  longer stored in the plist, other environment variables are kept), restarts
  them;
- points Hermes hooks (`~/.hermes/config.yaml` and
  `~/.hermes/shell-hooks-allowlist.json`) and the OpenClaw plugin link at the
  new copy.

Every changed file is backed up first. To undo:

```bash
bridge/codex-pet-bridge/tools/mac/install.sh --rollback ~/.codex-pet-bridge/backup/original
```

Check: both LaunchAgents `state = running`, `/health` answers, `/status` works
with the token (Step 0 commands). If Hermes or OpenClaw were already open,
ask the user to restart them so they load the new hook paths.

Claude Code needs no setup: the bridge reads its session transcripts in
`~/.claude/projects`. Only the "waiting for your approval" state needs Claude
Code hooks (see the bridge README); do not edit `~/.claude/settings.json`
without asking.

## Step 2 — firmware

Work in `firmware/agent_pet_display`. Keep build output out of iCloud Drive:

```bash
export PLATFORMIO_BUILD_DIR="$HOME/.cache/rlcd-agent-pet/build"
export PLATFORMIO_LIBDEPS_DIR="$HOME/.cache/rlcd-agent-pet/libdeps"
```

### 2a. Settings file

Pick one; neither prints the values.

```bash
# An older checkout still has the real values inside its include/app_config.h:
tools/import-legacy-secrets.sh /path/to/old/firmware/agent_pet_display/include/app_config.h

# A fresh setup: the user types the Wi-Fi name and password in the terminal;
# the bridge URL is built from this Mac's LAN address and the token file.
tools/make-secrets.sh            # or: tools/make-secrets.sh --ip 192.168.1.23
```

`import-legacy-secrets.sh` reports whether the board's token matches
`~/.codex-pet-bridge/token`. A mismatch means the board would get HTTP 401.

### 2b. Test and build (no flashing yet)

```bash
pio test -e native            # 108 tests, all must pass
pio run -e waveshare_rlcd     # expect RAM ~20 %, Flash ~31 %
```

### 2c. Flash (ask first)

```bash
pio device list               # find the port with 303A:1001
pio run -e waveshare_rlcd -t upload --upload-port /dev/cu.usbmodem101
```

Check through the bridge, not the serial port: after about 10 seconds
`/status` shows the board online with `firmware` `1.3.0`, battery, temperature,
Wi-Fi signal and `timeValid: true`. Opening the serial monitor
(`pio device monitor -b 115200`) resets the board; that is harmless.

On the first boot of 1.3.0 the board converts its RTC from the old China-time
convention to UTC, syncs with NTP and takes the time zone from the Mac.

## Step 3 — PetBar menu bar app (ask first)

```bash
bridge/codex-pet-bridge/macos/PetBar/build.sh --install
```

It compiles with `swiftc` into `$TMPDIR/petbar-build` (codesign rejects files
inside iCloud Drive), signs ad hoc, copies the app to `~/Applications` and
starts it. Check: `pgrep -lx PetBar` shows it and the pet icon is in the menu
bar. Starting it at login is the user's choice: the menu item 登录时启动.

## Step 4 — hand over

Tell the user:

- the dashboard is at `http://127.0.0.1:17366/ui/`; PetBar's 打开控制台…
  opens it with the token filled in;
- clock faces, 12/24 h, seconds and the board page can be changed there, in
  PetBar or with the buttons (BOOT: next page; KEY on the clock page: next
  face);
- the easter egg: drop a local video (for example their own copy of
  *Bad Apple!!*) into 彩蛋工坊, convert, then ▶ 板子播放. Holding BOOT + KEY
  for 2 s on the board plays the default animation.

## Troubleshooting

| Symptom | Likely cause and fix |
| --- | --- |
| LaunchAgent `last exit code = 78: EX_CONFIG` | Paths in the plist no longer exist. Run Step 1. |
| Board shows bridge offline, Wi-Fi fine | Mac address changed (fix the DHCP reservation, or re-create `secrets.h` and reflash), bridge not running, or token mismatch (HTTP 401 in the bridge logs). |
| Board shows Wi-Fi retrying | Wrong Wi-Fi name/password or a 5 GHz-only network. Re-create `secrets.h` and reflash. |
| `/status` returns 403 | The request came from another web page or through an unexpected host name. Use `127.0.0.1`, `localhost`, the LAN IP or the `.local` name. |
| Dashboard asks for a token | Open it from PetBar, or paste the token into the dialog (it stays in that browser only). |
| `bin/pio: bad interpreter` | The PlatformIO venv was moved. Use `<venv>/bin/python -m platformio`. |
| `codesign ... resource fork, Finder information, or similar detritus not allowed` | Building inside iCloud Drive. `build.sh` builds in `$TMPDIR` for this reason; keep `PETBAR_BUILD_DIR` outside iCloud. |
| Clock shows the wrong time zone | The board follows `/etc/localtime` of the Mac. An override can be set in the dashboard (时区). |
| Easter egg stops with STREAM STALLED | Wi-Fi too weak or the bridge stopped. Try again closer to the router; the board falls back to the built-in animation if nothing can be streamed. |

## Uninstall

Ask the user first; this removes the bridge and its history.

```bash
for L in net.vcxzvfe.codex-pet-bridge net.vcxzvfe.codex-pet-agent-sync; do
  launchctl bootout "gui/$(id -u)/$L" 2>/dev/null
  rm -f ~/Library/LaunchAgents/$L.plist
done
pkill -x PetBar; rm -rf ~/Applications/PetBar.app
# ~/.codex-pet-bridge holds the token, the event log and backups: remove only if the user wants to.
```
