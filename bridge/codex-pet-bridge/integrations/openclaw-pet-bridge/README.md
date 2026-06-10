# OpenClaw Pet Bridge Plugin

This plugin forwards official OpenClaw lifecycle hooks into `codex-pet-bridge`.

## What it emits

- `before_agent_start` -> `thinking`
- `before_tool_call` -> `searching` or `tool-use`
- `after_tool_call` -> `thinking`
- `after_tool_call` with approval-pending tool results -> `needs-attention`
- `agent_end` -> `completed` or `error`

## Install

1. Copy or symlink this directory into `~/.openclaw/extensions/openclaw-pet-bridge`.
2. Allow the plugin in your OpenClaw config.
3. Point it at the bridge with either:
   - `plugins.entries.openclaw-pet-bridge.config.bridgeUrl`
   - or `PET_BRIDGE_URL`

Optional config fields:

- `source`
- `taskPrefix`
- `notifyOnCompleted`
- `notifyOnError`

If the bridge requires auth, export `PET_BRIDGE_TOKEN`.
