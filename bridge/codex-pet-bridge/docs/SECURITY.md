# Security

This project is meant to be run on trusted machines and small private networks. Treat it as a local notification hub, not a public internet service.

## Defaults

- The bridge listens on `127.0.0.1` by default.
- Raw upstream payloads are not stored by default.
- If `PET_BRIDGE_HOST` is set to a non-loopback address and `PET_BRIDGE_TOKEN` is not set, the server refuses to start.
- Request bodies are capped by `PET_BRIDGE_MAX_BODY_BYTES`.
- Notification messages are truncated by `PET_BRIDGE_MAX_MESSAGE_CHARS`.
- Optional outbound sinks use short timeouts and a bounded local outbox, so webhook outages do not block event ingestion.

## Recommended Mac mini Setup

Run the bridge on the Mac mini bound to localhost:

```bash
PET_BRIDGE_HOST=127.0.0.1 node ./src/bridge-server.js
```

From the MacBook Pro, use SSH port forwarding:

```bash
ssh -N -L 17366:127.0.0.1:17366 mac-mini
```

Claude Code hooks on the MacBook can keep posting to:

```text
http://127.0.0.1:17366/events
```

The TCP connection is forwarded through SSH, so the bridge does not need to be exposed on the LAN.

## Token Mode

If you intentionally expose the bridge to the LAN, set a random token:

```bash
PET_BRIDGE_HOST=0.0.0.0 PET_BRIDGE_TOKEN="$(openssl rand -hex 32)" node ./src/bridge-server.js
```

Clients can authenticate with one of:

```http
Authorization: Bearer <token>
x-pet-bridge-token: <token>
```

For ESP S3 devices that cannot easily set headers:

```http
GET /esp32/poll?token=<token>
```

## Secrets

Do not put API keys directly into event messages. OpenClaw or other adapter credentials should live in adapter-specific environment variables on the Mac mini, not on the MacBook Pro.

If you enable raw payload storage with `PET_BRIDGE_STORE_RAW=1`, the bridge redacts common secret-like fields such as `token`, `secret`, `password`, `authorization`, and `api_key`, but raw storage should still be treated as sensitive.

Generated state files such as `bridge-state.json`, `events.jsonl`, and `~/.codex-pet-bridge/*.jsonl` should be treated as local operational data and kept out of Git.

## XiaoZhi Assistant Sink

`XIAOZHI_ASSISTANT_URL` should normally point at `http://127.0.0.1:8003` on the Mac mini or at a trusted LAN address. The bridge only sends normalized notification fields and does not forward raw payloads to XiaoZhi.

If the assistant hub later requires authentication, set:

```bash
XIAOZHI_WEBHOOK_TOKEN="<token>"
```

The token is sent as `Authorization: Bearer <token>`. Do not put this token in Git-tracked `.env` files.

## Not Yet Implemented

- Persistent encrypted notification storage
- HTTPS termination
- User accounts or multi-tenant access control
- Replay protection beyond simple token checks

For the intended home-lab setup, SSH tunneling plus localhost binding is the preferred security model.
