# Signaling server

40-line WebSocket relay that pairs two WebRTC peers by room code and
forwards their offer/answer SDPs. Holds no game state; once the peers
connect directly, they stop talking to this server.

## Run locally

```
cd server
npm install
PORT=7863 node signaling.js
```

## Deploy behind Caddy + TLS

Browsers served over HTTPS can only open `wss://` sockets. Add a handler
to the existing Caddyfile block for the site; Caddy auto-upgrades HTTP
to WebSocket on `reverse_proxy` with no extra directives.

```caddyfile
tallec.freeboxos.fr {
    # ... existing site config (root, file_server, etc.) ...

    handle /signal* {
        reverse_proxy localhost:7863
    }
}
```

Reload with `caddy reload --config /etc/caddy/Caddyfile`. The client will
then connect to `wss://tallec.freeboxos.fr/signal` automatically — the
URL is derived from `location.protocol` in `multiplayer_menu.cpp`.

## systemd unit

Drop into `/etc/systemd/system/minecraft-signaling.service`:

```ini
[Unit]
Description=Minecraft WebRTC signaling relay
After=network.target

[Service]
User=YOUR_USER
WorkingDirectory=/home/YOUR_USER/minecraft-signaling
Environment=PORT=7863
ExecStart=/usr/bin/node signaling.js
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Then: `sudo systemctl daemon-reload && sudo systemctl enable --now minecraft-signaling`.

## Protocol

All messages are JSON objects with a `type` discriminator.

- `{type:"host"}` → server responds `{type:"code", code:"ABC123"}`
- `{type:"join", code:"ABC123"}` → server responds `{type:"joined"}`
  and tells host `{type:"peer-joined"}`
- `{type:"offer", sdp:"..."}` → forwarded to peer
- `{type:"answer", sdp:"..."}` → forwarded to peer
- Errors: `{type:"error", error:"..."}`

Rooms expire after 5 minutes of inactivity; closing either socket tears
down the pair immediately.
