<div align="center">

# Codex Pet Bridge

**Local-first status bridge for Codex, Claude Code, desktop pets, and XiaoZhi devices.**

One small hub. Every agent state, visible.

English · [中文](README.zh-CN.md)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Node.js 20+](https://img.shields.io/badge/Node.js-20%2B-339933?logo=node.js&logoColor=white)](https://nodejs.org/)
[![Status: Alpha](https://img.shields.io/badge/status-alpha-yellow)](#project-status)
[![Local-first](https://img.shields.io/badge/local--first-yes-2ea44f)](#security-model)

</div>

---

## The Problem

Long-running coding agents are useful only if you notice the right moment to step back in. Codex, Claude Code, Claude Desktop, OpenClaw, and custom automations may all be running on different machines, but their status usually stays trapped inside each app window.

That creates three practical problems:

1. **Attention is fragmented**: one task waits in Codex, another needs approval in Claude Code, and neither is visible from the room.
2. **Integrations are brittle**: patching app bundles or scraping UI state breaks whenever upstream tools update.
3. **Physical indicators need a stable contract**: a desktop pet, ESP32 screen, or XiaoZhi robot should consume the same clean event model instead of learning every upstream payload shape.

Codex Pet Bridge is a local notification hub for that missing layer.

## What It Does

- Normalizes upstream agent signals into a stable `PetEvent` model.
- Builds an unread `PetNotification` queue for events that need human attention.
- Streams live state to a desktop pet UI over Server-Sent Events.
- Exposes a compact polling API for ESP32 / XiaoZhi-style devices.
- Forwards semantic status events to the XiaoZhi Assistant Hub on a Mac mini.
- Writes JSONL logs for debugging without storing raw prompts by default.
- Stays local-first: localhost by default, token-protected if exposed to a LAN.

## Architecture

```mermaid
flowchart LR
  CC["Claude Code CLI"] -->|"hooks: command/http"| Bridge["codex-pet-bridge"]
  CD["Claude Desktop / Claude Code"] -->|"MCP tool: pet_emit_event"| Bridge
  CX["Codex CLI / Desktop"] -->|"notify wrapper / plugin / App Server adapter"| Bridge
  OC["OpenClaw or custom agents"] -->|"POST /events"| Bridge
  Bridge -->|"SSE: GET /stream"| Pet["Desktop Pet UI"]
  Bridge -->|"GET /esp32/poll"| ESP["ESP32 / XiaoZhi display"]
  Bridge -->|"POST /assistant/notifications"| XiaoZhi["XiaoZhi Assistant Hub"]
  Bridge -->|"POST webhook"| Push["Push or automation sink"]
  Bridge -->|"JSONL"| Log["events.jsonl"]
```

The bridge deliberately avoids patching Codex Desktop, Claude Desktop, Claude Code, or XiaoZhi firmware. Each integration enters through a public hook, MCP tool, webhook, plugin, or polling adapter, then becomes one internal event shape.

XiaoZhi integration is community/home-lab integration work, not an official XiaoZhi product or endorsement.

## Quick Start

```bash
git clone https://github.com/vcxzvfe/codex-pet-bridge.git
cd codex-pet-bridge
npm run start
```

Default URL:

```text
http://127.0.0.1:17366
```

Send a test event:

```bash
curl -sS http://127.0.0.1:17366/events \
  -H 'content-type: application/json' \
  -d '{
    "source": "codex",
    "task": "demo-runtime",
    "status": "running",
    "message": "Codex is working on the demo"
  }'
```

## API Surface

| Endpoint | Purpose |
| --- | --- |
| `POST /events` | Ingest one normalized or semi-raw upstream event. |
| `GET /events` | Read recent events. |
| `GET /state` | Read the latest event and unread count. |
| `GET /stream` | Subscribe to live events via SSE. |
| `GET /notifications` | Read unread notifications. |
| `GET /notifications/next` | Read the next unread notification. |
| `POST /notifications/:id/ack` | Mark one notification as read. |
| `POST /notifications/ack-all` | Mark every notification as read. |
| `GET /esp32/poll` | Compact polling endpoint for ESP32 / XiaoZhi devices. Also takes board telemetry and returns display settings, the time zone and easter-egg commands. |
| `GET /esp32/anim/:id/frames` | Streams RLA1 animation frames to the board in small chunks. |
| `GET /status` | Everything the dashboard and the menu bar app show: agents, usage, board telemetry, settings, animations. |
| `GET` / `POST /settings` | Clock face, 12/24 h, seconds, one-shot page switch, time zone override, default animation. |
| `GET /anim`, `GET` / `PUT` / `DELETE /anim/:id` | Animation library (RLA1 files uploaded from the dashboard). |
| `POST /egg/play`, `POST /egg/stop` | Schedule or stop an animation on the board. |
| `GET /ui/` | The RLCD dashboard (static files; its API calls need the token). |
| `GET /health` | Health check. |

## Event Model

`PetEvent` is the full live feed. It is useful for animation, diagnostics, logs, and downstream adapters.

```json
{
  "source": "laptop-codex",
  "task": "laptop-codex-runtime",
  "status": "running",
  "message": "Laptop Codex task is running",
  "workspace": "/path/to/project",
  "sessionId": "optional-upstream-session"
}
```

`PetNotification` is the intervention queue. By default, the bridge queues:

```text
needs-attention, completed, near-complete, error
```

You can change that with:

```bash
PET_NOTIFY_STATUSES=needs-attention,completed,error npm run start
```

Events with the same source, task, session, workspace, status, and message are throttled for 15 seconds by default:

```bash
PET_NOTIFY_THROTTLE_MS=30000 npm run start
```

## Boundaries

The bridge is intentionally narrow:

- It does not render a pet UI.
- It does not choose XiaoZhi screen colors, brightness, or night-mode policy.
- It does not patch upstream app bundles.
- It does not store full raw upstream payloads unless explicitly configured.

Those responsibilities belong to downstream UIs, the XiaoZhi backend, official upstream extension points, or adapter-specific code.

## Claude Code CLI

Add `pet-claude-hook` as an observational hook in user-level `~/.claude/settings.json` or project-level `.claude/settings.json`.

```json
{
  "hooks": {
    "Notification": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "node /ABS/PATH/TO/src/claude-hook.js"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "node /ABS/PATH/TO/src/claude-hook.js"
          }
        ]
      }
    ],
    "Stop": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "node /ABS/PATH/TO/src/claude-hook.js"
          }
        ]
      }
    ]
  }
}
```

Recommended starter events are `Notification`, `UserPromptSubmit`, and `Stop`. They are enough for "thinking / waiting for you / completed" without flooding the pet or XiaoZhi screen with every tool call.

Hook failures exit with code `0`; Claude Code should never be blocked because the pet bridge is offline. Failed sends are written to the same bounded `PET_NOTIFY_QUEUE` used by `pet-notify`, and `pet-notify --flush` retries them later.

Hooks are optional when the bridge runs on the same Mac as Claude Code: the bridge reads Claude Code's own session transcripts (`~/.claude/projects/*/*.jsonl`, or `$CLAUDE_CONFIG_DIR/projects`) to tell thinking, tool use, searching and finished turns apart, and to show today's tokens, the context window and the model. Nothing in Claude Code's configuration changes. Hooks remain the only way to see "waiting for your approval". Set `PET_BRIDGE_CLAUDE_TRANSCRIPTS=0` to turn the transcript reader off.

## Claude Desktop / MCP

The stdio MCP server exposes one tool:

- `pet_emit_event`: send a status bubble or notification event to the bridge.

Claude Code:

```bash
claude mcp add --transport stdio codex-pet-bridge -- node /ABS/PATH/TO/src/mcp-server.js
```

Claude Desktop:

```json
{
  "mcpServers": {
    "codex-pet-bridge": {
      "type": "stdio",
      "command": "node",
      "args": ["/ABS/PATH/TO/src/mcp-server.js"],
      "env": {
        "PET_BRIDGE_URL": "http://127.0.0.1:17366/events"
      }
    }
  }
}
```

## Codex

Codex integration should stay thin and local. Today, the practical paths are:

- Use `pet-notify` from a Codex notify wrapper when a turn ends.
- Run `pet-agent-sync` as a lightweight activity bridge for "running" state.
- Add a Codex plugin or App Server adapter later when the public extension point is stable.

`pet-notify` has a bounded disk queue. If the bridge is offline, the hook exits quickly and retries later instead of blocking Codex:

```bash
pet-notify \
  --source laptop-codex \
  --task laptop-codex-runtime \
  --status completed \
  --message "Codex task completed" \
  --notify
```

`pet-agent-sync` can run once from cron/launchd or stay resident:

```bash
PET_AGENT_SYNC_PREFIX=laptop pet-agent-sync --watch
```

It reads recent Codex session activity and lightweight local process state. The defaults are intentionally conservative; official Codex hooks or plugins should replace this adapter when they are available.

Example running event:

```bash
curl -sS http://127.0.0.1:17366/events \
  -H 'content-type: application/json' \
  -d '{
    "source": "laptop-codex",
    "task": "laptop-codex-runtime",
    "status": "running",
    "message": "Codex is working"
  }'
```

Example completion event:

```bash
curl -sS http://127.0.0.1:17366/events \
  -H 'content-type: application/json' \
  -d '{
    "source": "laptop-codex",
    "task": "laptop-codex-runtime",
    "status": "completed",
    "message": "Codex task completed",
    "notify": true
  }'
```

## Hermes

Hermes already has an official shell-hook surface. `pet-hermes-hook` is a thin observer that maps Hermes hook events into bridge events without patching Hermes itself.

Add these hooks to `~/.hermes/config.yaml`:

```yaml
hooks:
  pre_llm_call:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
  pre_tool_call:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
  post_tool_call:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
  post_llm_call:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
  pre_approval_request:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
  post_approval_response:
    - command: "node /ABS/PATH/TO/src/hermes-hook.js"
```

Recommended behavior:

- `pre_llm_call` -> `thinking`
- `pre_tool_call` -> `searching` or `tool-use`
- `post_tool_call` -> back to `thinking`
- `pre_approval_request` -> `needs-attention`
- `post_llm_call` -> `completed`

The hook stays observational: send failures are queued and Hermes continues running normally.

For Hermes Desktop or gateway paths that do not fire shell hooks, keep `pet-agent-sync --watch` running as a fallback. It tails `~/.hermes/logs/agent.log`, detects `conversation turn` and `Turn ended` records, then emits a stable `hermes` slot with the upstream session id. This is intentionally less detailed than hooks: it can reliably show `thinking` and `completed`, while tool-level states such as `searching` and `tool-use` still require hooks.

```bash
PET_AGENT_SYNC_HERMES_LOG="$HOME/.hermes/logs/agent.log" pet-agent-sync --watch
```

## OpenClaw

OpenClaw's official plugin hooks are the best fit for live status. This repo ships a small plugin under `integrations/openclaw-pet-bridge/`.

Suggested install flow:

1. Copy or symlink `integrations/openclaw-pet-bridge` into `~/.openclaw/extensions/openclaw-pet-bridge`.
2. Allow that plugin in your OpenClaw config.
3. Set `plugins.entries.openclaw-pet-bridge.config.bridgeUrl` to your bridge URL, or export `PET_BRIDGE_URL`.

The plugin emits:

- `before_agent_start` -> `thinking`
- `before_tool_call` -> `searching` or `tool-use`
- `after_tool_call` -> back to `thinking`
- `after_tool_call` with approval-pending results -> `needs-attention`
- `agent_end` -> `completed` or `error`

If your bridge listens on a non-loopback address, export `PET_BRIDGE_TOKEN` for the plugin process so it can authenticate.

## Desktop Pet UI

Subscribe to the SSE stream:

```js
const stream = new EventSource("http://127.0.0.1:17366/stream");

stream.onmessage = (event) => {
  const petEvent = JSON.parse(event.data);
  renderPetReaction(petEvent.status, petEvent.message);
};

stream.addEventListener("notification", (event) => {
  const notification = JSON.parse(event.data);
  showUnreadBubble(notification.title, notification.message);
});
```

If your pet already has an HTTP receiver:

```bash
PET_WEBHOOK_URL=http://127.0.0.1:3000/pet/events npm run start
```

If you only want important notifications:

```bash
PET_NOTIFICATION_WEBHOOK_URL=http://127.0.0.1:3000/notify npm run start
```

## XiaoZhi Assistant Hub

If the Mac mini already runs a XiaoZhi Assistant Hub, prefer forwarding semantic events to that service instead of creating a second device-specific status server.

```bash
XIAOZHI_ASSISTANT_URL=http://127.0.0.1:8003 \
XIAOZHI_SOURCE_PREFIX=laptop \
npm run start
```

The bridge sends:

```text
POST <XIAOZHI_ASSISTANT_URL>/assistant/notifications
```

Payload shape:

```json
{
  "source": "laptop-codex",
  "task": "laptop-codex-runtime",
  "status": "running",
  "message": "Laptop Codex task is running",
  "priority": "normal",
  "needs_user": false
}
```

Status mapping:

| Bridge status | XiaoZhi status |
| --- | --- |
| `thinking`, `working`, `started`, `running`, `progress`, `near-complete` | `running` |
| `completed`, `done`, `success`, `finished` | `done` |
| `needs-attention`, `waiting-user` | `waiting_user` |
| `error`, `failed`, `blocked` | `error` |
| `idle`, `clear`, `ack`, `dismissed` | `clear` |

The XiaoZhi backend owns final screen behavior. A typical downstream policy is: active tasks wake the screen even during night mode; Codex uses blue-purple; Claude Code uses orange; OpenClaw uses teal-green; completion briefly flashes green at high brightness; idle returns to the configured day/night screen schedule.

Recommended source labels:

| Machine / tool | Source |
| --- | --- |
| Laptop Codex | `laptop-codex` |
| Hub Codex | `hub-codex` |
| Windows Codex | `win-codex` |
| Laptop Claude Code | `laptop-claude` |
| Hub Claude Code | `hub-claude` |
| OpenClaw | `openclaw` |

## RLCD Dashboard, Menu Bar App and Easter Egg

The bridge serves a dashboard at `http://127.0.0.1:17366/ui/`:

- what every agent is doing, with Claude Code's current tool, model and context
- the board's battery, temperature, humidity, Wi-Fi signal, firmware and page
- the seven clock faces, rendered by the firmware's own drawing code, plus 12/24 h, seconds and time zone
- an easter-egg studio: pick a local video (for example your own copy of *Bad Apple!!*), convert it to 1-bit frames in the browser, upload the result and play it on the board, optionally with the video's sound playing in sync on the Mac

Videos are decoded in the browser; only the 1-bit RLA1 frames reach the bridge, which keeps them in `PET_BRIDGE_ANIM_DIR` (default: `anim/` next to the state file). On the board, holding BOOT and KEY together for 2 s (or BOOT alone for 5 s) plays the default animation, or a built-in one when nothing is uploaded.

`macos/PetBar` is a small menu bar app with the same information and the page, clock face and easter-egg controls. Build it with `macos/PetBar/build.sh --install` (needs the Xcode Command Line Tools).

`tools/mac/install.sh` installs or repairs the bridge on a Mac: it copies the bridge to `~/.codex-pet-bridge/app`, keeps the token in `~/.codex-pet-bridge/token`, rewrites the LaunchAgents and points Hermes hooks and the OpenClaw plugin at the copy. It is a dry run unless you pass `--apply`, backs up every file it changes and has `--rollback`.

## ESP32 / XiaoZhi Polling

For simple firmware or prototype screens, use HTTP polling:

```http
GET http://127.0.0.1:17366/esp32/poll
```

Response:

```json
{
  "ok": true,
  "unread_count": 1,
  "current_status": "completed",
  "notification": {
    "id": "uuid",
    "source": "claude-code",
    "task": "project-name",
    "status": "completed",
    "priority": 1,
    "title": "Task completed",
    "message": "Claude Code task completed",
    "project": "/path/to/project",
    "time": "2026-05-03T12:00:00.000Z"
  }
}
```

Ack after display or playback:

```http
POST http://127.0.0.1:17366/notifications/<id>/ack
```

If the device can only send GET:

```http
GET http://127.0.0.1:17366/esp32/poll?ack=<id>
```

The RLCD firmware (1.3.0 and later) also reports telemetry in the same request, for example `&fw=1.3.0&bat=84&mv=3980&temp=22.6&hum=48&rssi=-52&up=3600&page=clock&style=segment&srev=…&erev=…&heap=…&clk=1&eggreq=…`, and the response carries a few extra fields that older firmware ignores:

```json
{
  "server_time_ms": 1790671923000,
  "display": { "rev": 1790671900, "clock_style": "segment", "hour12": false, "show_seconds": true, "page": "clock" },
  "time": { "tz": "GMT0BST,M3.5.0/1,M10.5.0", "zone": "Europe/London" },
  "egg": { "rev": 1790671920, "id": "badapple", "frames": 4382, "fps_x100": 2000, "width": 400, "height": 300, "start_at_ms": 1790671926500 }
}
```

`display.page` is sent once per revision; the time zone follows the Mac (`/etc/localtime`) unless the dashboard sets an override.

## Security Model

This is meant for trusted local machines and home-lab networks, not the public internet.

- Binds to `127.0.0.1` by default.
- Refuses non-loopback listening unless `PET_BRIDGE_TOKEN` is set.
- Supports `Authorization: Bearer <token>`, `x-pet-bridge-token`, or `?token=...`.
- Reads the token from `PET_BRIDGE_TOKEN` or the file named by `PET_BRIDGE_TOKEN_FILE`. Hooks, `pet-notify` and the OpenClaw plugin also look in `~/.codex-pet-bridge/token`, so the secret lives in one `0600` file instead of several configs.
- The dashboard API (`/status`, `/settings`, `/anim`, `/egg`) answers only same-origin requests addressed to an IP, `localhost`, a `.local` name or this Mac's host name, so other web pages cannot read it or reach it through DNS rebinding.
- Does not store raw upstream payloads unless `PET_BRIDGE_STORE_RAW=1`.
- Redacts common secret-like fields if raw storage is enabled.
- Keeps hooks observational so upstream tools continue working if the bridge is offline.

The preferred multi-machine setup is Mac mini as the always-on hub plus SSH tunnels from laptops. See [Mac mini Deployment](docs/MAC_MINI_DEPLOYMENT.md).

## Project Status

> Alpha. Useful for local experiments, but API details may still change.

Verified in the current codebase:

- HTTP event ingestion
- SSE live stream
- unread notification queue and ack
- bounded client-side `pet-notify` queue
- bounded bridge sink outbox
- Claude Code command hook
- lightweight Codex/Claude activity sync helper
- MCP stdio tool
- ESP32 polling endpoint
- XiaoZhi Assistant Hub sink
- localhost-first security guard

Experimental or deployment-specific:

- official Codex plugin/App Server adapter
- richer desktop pet UI examples
- persistent encrypted notification storage
- packaged installers

## Compatibility

| Integration | Current expectation |
| --- | --- |
| Node.js | 20 or later |
| Claude Code | command hooks and stdio MCP |
| Claude Desktop | stdio MCP configuration |
| Codex | notify wrapper, local process bridge, or future plugin/App Server adapter |
| XiaoZhi | Assistant Hub endpoint compatible with `/assistant/notifications` |
| ESP32 | HTTP polling against `/esp32/poll` |

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Security](docs/SECURITY.md)
- [Mac mini Deployment](docs/MAC_MINI_DEPLOYMENT.md)
- [References](docs/REFERENCES.md)

## Validation

```bash
npm run smoke
```

or:

```bash
node ./test/smoke.js
```

The full suite (`node --test`) covers the board protocol, settings, animation store and dashboard API. `node tools/ui-e2e.mjs <video>` drives the dashboard in Chromium (needs `playwright`), and `tools/mac/test-install.sh` runs the Mac installer against a throwaway home directory.

## Contributing

Issues and PRs are welcome. Good first contributions include new adapters, desktop pet UI examples, documentation fixes, and test coverage for real multi-machine setups.

Please keep integrations thin: use official hooks, MCP, plugins, local HTTP, or polling APIs; do not patch upstream app bundles.

## License

[MIT](LICENSE) © 2026 vcxzvfe and Codex Pet Bridge contributors.
