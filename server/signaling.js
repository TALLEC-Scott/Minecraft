#!/usr/bin/env node
// Minimal WebSocket signaling server for WebRTC multiplayer.
// Pairs two peers via a 6-char room code and forwards their offer/answer
// SDP blobs. Holds no game state — once the peers connect directly, they
// stop talking to this server.
//
// Usage: PORT=7863 node signaling.js
//        (behind nginx TLS reverse-proxy, or with --cert/--key flags)

const http = require('http');
const WebSocket = require('ws');

const PORT = parseInt(process.env.PORT || '7863', 10);
const ROOM_CODE_LEN = 6;
const ROOM_TTL_MS = 5 * 60 * 1000;   // rooms expire after 5 min
const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no 0/O/1/I collisions

const rooms = new Map(); // code -> { host, guest, createdAt }

function randomCode() {
    let s = '';
    for (let i = 0; i < ROOM_CODE_LEN; i++) {
        s += ALPHABET[Math.floor(Math.random() * ALPHABET.length)];
    }
    return s;
}

function removeRoom(code) {
    const r = rooms.get(code);
    if (!r) return;
    try { if (r.host && r.host.readyState === WebSocket.OPEN) r.host.close(); } catch {}
    try { if (r.guest && r.guest.readyState === WebSocket.OPEN) r.guest.close(); } catch {}
    rooms.delete(code);
}

// Periodic expiry sweep — keeps the map bounded even if clients leak sockets.
setInterval(() => {
    const now = Date.now();
    for (const [code, r] of rooms) {
        if (now - r.createdAt > ROOM_TTL_MS) removeRoom(code);
    }
}, 30 * 1000);

const server = http.createServer((req, res) => {
    // Simple health check so a reverse proxy / uptime monitor can ping.
    if (req.url === '/health') {
        res.writeHead(200, {'Content-Type': 'text/plain'});
        res.end(`ok\nrooms=${rooms.size}\n`);
        return;
    }
    res.writeHead(404); res.end();
});

const wss = new WebSocket.Server({ server });

function send(ws, obj) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

wss.on('connection', (ws) => {
    ws._code = null;
    ws._role = null;

    ws.on('message', (raw) => {
        let msg;
        try { msg = JSON.parse(raw.toString()); } catch { return; }

        if (msg.type === 'host') {
            // Retry a few times in the astronomically-unlikely case of
            // colliding codes; 32^6 = ~1B so collisions are rare.
            let code;
            for (let i = 0; i < 5; i++) {
                code = randomCode();
                if (!rooms.has(code)) break;
            }
            if (rooms.has(code)) { send(ws, {type:'error', error:'no free codes'}); return; }
            rooms.set(code, { host: ws, guest: null, createdAt: Date.now() });
            ws._code = code; ws._role = 'host';
            send(ws, { type: 'code', code });
            return;
        }

        if (msg.type === 'join') {
            const room = rooms.get(msg.code);
            if (!room) { send(ws, {type:'error', error:'room not found'}); return; }
            if (room.guest) { send(ws, {type:'error', error:'room full'}); return; }
            room.guest = ws;
            ws._code = msg.code; ws._role = 'guest';
            send(ws, { type: 'joined' });
            send(room.host, { type: 'peer-joined' });
            return;
        }

        // Offer / answer / generic forward: blindly relay to the other side.
        const room = rooms.get(ws._code);
        if (!room) return;
        const other = ws._role === 'host' ? room.guest : room.host;
        if (other && other.readyState === WebSocket.OPEN) {
            other.send(raw.toString());
        }
    });

    ws.on('close', () => {
        if (!ws._code) return;
        const room = rooms.get(ws._code);
        if (!room) return;
        // Closing one peer tears down the other too — they can't make
        // progress alone. The next Host/Join attempt starts fresh.
        removeRoom(ws._code);
    });
});

server.listen(PORT, () => {
    console.log(`[signaling] listening on :${PORT}`);
});
