#include "net/net_session.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "net/net_protocol.h"
#include "player.h"
#include "world.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {

constexpr double BROADCAST_INTERVAL = 0.1; // 10 Hz
constexpr double REMOTE_TIMEOUT = 10.0;    // drop silent peers after 10s

#ifdef __EMSCRIPTEN__
uint32_t genPeerId() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(1u, 0xFFFFFFFEu);
    return dist(rng);
}
#endif

} // namespace

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------------
// Emscripten/WebRTC glue. We keep an array of RTCPeerConnection objects in
// JS and reference them from C++ by integer handle. Inbound binary messages
// are pushed into a JS queue; poll() drains it.
// ---------------------------------------------------------------------------
// clang-format off
EM_JS(void, netjs_init_globals, (), {
    if (typeof Module !== "object") return;
    if (Module._netState) return;
    Module._netState = {peers : [], nextId : 0};
    Module._netInbox = [];
});

EM_JS(int, netjs_create_host, (), {
    if (!Module._netState) Module._netState = {peers : [], nextId : 0};
    var s = Module._netState;
    var id = s.nextId++;
    var pc = new RTCPeerConnection({iceServers : [ {urls : "stun:stun.l.google.com:19302"} ]});
    var ch = pc.createDataChannel("game", {ordered : true});
    var rec = {pc : pc, ch : ch, role : "host", offerSdp : "", answerSdp : "", ready : false};
    ch.binaryType = "arraybuffer";
    ch.onopen = function() {
        rec.ready = true;
    };
    ch.onclose = function() {
        rec.ready = false;
    };
    ch.onmessage = function(ev) {
        if (!Module._netInbox) Module._netInbox = [];
        var data = ev.data;
        if (data instanceof ArrayBuffer) {
            Module._netInbox.push(new Uint8Array(data));
        } else if (ArrayBuffer.isView(data)) {
            Module._netInbox.push(new Uint8Array(data.buffer.slice(0)));
        }
    };
    pc.onicegatheringstatechange = function() {
        if (pc.iceGatheringState === "complete" && pc.localDescription) {
            rec.offerSdp = JSON.stringify(pc.localDescription);
        }
    };
    pc.createOffer().then(function(offer) { return pc.setLocalDescription(offer); });
    s.peers[id] = rec;
    return id;
});

EM_JS(int, netjs_create_client, (const char* offerJson), {
    if (!Module._netState) Module._netState = {peers : [], nextId : 0};
    var s = Module._netState;
    var id = s.nextId++;
    var pc = new RTCPeerConnection({iceServers : [ {urls : "stun:stun.l.google.com:19302"} ]});
    var rec = {pc : pc, ch : null, role : "client", offerSdp : "", answerSdp : "", ready : false};
    pc.ondatachannel = function(ev) {
        var ch = ev.channel;
        ch.binaryType = "arraybuffer";
        rec.ch = ch;
        ch.onopen = function() {
            rec.ready = true;
        };
        ch.onclose = function() {
            rec.ready = false;
        };
        ch.onmessage = function(mev) {
            if (!Module._netInbox) Module._netInbox = [];
            var data = mev.data;
            if (data instanceof ArrayBuffer) {
                Module._netInbox.push(new Uint8Array(data));
            } else if (ArrayBuffer.isView(data)) {
                Module._netInbox.push(new Uint8Array(data.buffer.slice(0)));
            }
        };
    };
    pc.onicegatheringstatechange = function() {
        if (pc.iceGatheringState === "complete" && pc.localDescription) {
            rec.answerSdp = JSON.stringify(pc.localDescription);
        }
    };
    var offerStr = UTF8ToString(offerJson);
    var offer;
    try {
        offer = JSON.parse(offerStr);
    } catch (e) {
        console.error("offer parse", e);
        return id;
    }
    pc.setRemoteDescription(offer).then(function() { return pc.createAnswer(); }).then(function(answer) {
        return pc.setLocalDescription(answer);
    });
    s.peers[id] = rec;
    return id;
});

EM_JS(const char*, netjs_get_local_sdp, (int handle), {
    var s = Module._netState;
    if (!s) return 0;
    var rec = s.peers[handle];
    if (!rec) return 0;
    var str = (rec.role === "host") ? rec.offerSdp : rec.answerSdp;
    if (!str) return 0;
    var lengthBytes = lengthBytesUTF8(str) + 1;
    var ptr = _malloc(lengthBytes);
    stringToUTF8(str, ptr, lengthBytes);
    return ptr;
});

EM_JS(void, netjs_accept_answer, (int handle, const char* answerJson), {
    var s = Module._netState;
    if (!s) return;
    var rec = s.peers[handle];
    if (!rec) return;
    var str = UTF8ToString(answerJson);
    var ans;
    try {
        ans = JSON.parse(str);
    } catch (e) {
        console.error("answer parse", e);
        return;
    }
    rec.pc.setRemoteDescription(ans);
});

EM_JS(int, netjs_is_ready, (int handle), {
    var s = Module._netState;
    if (!s) return 0;
    var rec = s.peers[handle];
    if (!rec) return 0;
    return rec.ready ? 1 : 0;
});

EM_JS(void, netjs_send, (int handle, const void* ptr, int len), {
    var s = Module._netState;
    if (!s) return;
    var rec = s.peers[handle];
    if (!rec || !rec.ch || !rec.ready) return;
    var view = HEAPU8.subarray(ptr, ptr + len);
    // Copy into a detached buffer — the WASM heap may move out from under
    // the datachannel send if memory growth resizes HEAPU8 mid-call.
    var copy = new Uint8Array(len);
    copy.set(view);
    try {
        rec.ch.send(copy.buffer);
    } catch (e) { /* channel closed */
    }
});

EM_JS(void, netjs_close, (int handle), {
    var s = Module._netState;
    if (!s) return;
    var rec = s.peers[handle];
    if (!rec) return;
    try {
        if (rec.ch) rec.ch.close();
    } catch (e) {
    }
    try {
        rec.pc.close();
    } catch (e) {
    }
    s.peers[handle] = null;
});

EM_JS(int, netjs_inbox_size, (), { return (Module._netInbox ? Module._netInbox.length : 0); });

// Pops the next inbox message into HEAPU8 at `ptr`. Returns the number of
// bytes written, 0 if empty, or -1 if the message doesn't fit in `cap`.
EM_JS(int, netjs_inbox_pop, (void* ptr, int cap), {
    if (!Module._netInbox || Module._netInbox.length === 0) return 0;
    var msg = Module._netInbox[0];
    if (msg.length > cap) return -1;
    HEAPU8.set(msg, ptr);
    Module._netInbox.shift();
    return msg.length;
});

// Signaling-server orchestration. Opens a WebSocket, pairs with a peer by
// room code, and drives the SDP exchange automatically so the user doesn't
// have to copy-paste anything. Produces a handle into the existing peer
// array, so netjs_is_ready / netjs_send / netjs_inbox_pop keep working
// unchanged once the data channel is up.
EM_JS(int, netjs_signaling_host, (const char* urlPtr), {
    var url = UTF8ToString(urlPtr);
    if (!Module._netState) Module._netState = {peers : [], nextId : 0};
    var s = Module._netState;
    var id = s.nextId++;
    var pc = new RTCPeerConnection({iceServers : [ {urls : "stun:stun.l.google.com:19302"} ]});
    var ch = pc.createDataChannel("game", {ordered : true});
    ch.binaryType = "arraybuffer";
    var rec = {pc : pc, ch : ch, role : "host", offerSdp : "", answerSdp : "", ready : false,
               sigCode : "", sigStatus : "connecting"};
    ch.onopen = function() { rec.ready = true; rec.sigStatus = "connected"; };
    ch.onclose = function() { rec.ready = false; };
    ch.onmessage = function(ev) {
        if (!Module._netInbox) Module._netInbox = [];
        var data = ev.data;
        if (data instanceof ArrayBuffer) Module._netInbox.push(new Uint8Array(data));
        else if (ArrayBuffer.isView(data)) Module._netInbox.push(new Uint8Array(data.buffer.slice(0)));
    };
    var ws = new WebSocket(url);
    rec.ws = ws;
    ws.onopen = function() {
        rec.sigStatus = "requesting room";
        ws.send(JSON.stringify({type : "host"}));
    };
    ws.onerror = function() { rec.sigStatus = "signaling error"; };
    ws.onclose = function() {
        // Only surface this as an error if the data channel never opened —
        // otherwise it's the normal teardown after pairing succeeded.
        if (!rec.ready && rec.sigStatus !== "connected") rec.sigStatus = "signaling closed";
    };
    ws.onmessage = function(ev) {
        var msg; try { msg = JSON.parse(ev.data); } catch (e) { return; }
        if (msg.type === "code") {
            rec.sigCode = msg.code;
            rec.sigStatus = "waiting for peer (code " + msg.code + ")";
        } else if (msg.type === "peer-joined") {
            rec.sigStatus = "negotiating";
            pc.createOffer().then(function(offer) { return pc.setLocalDescription(offer); });
        } else if (msg.type === "answer") {
            try { pc.setRemoteDescription(msg.sdp); } catch (e) { console.warn("setRemote", e); }
        } else if (msg.type === "error") {
            rec.sigStatus = "error: " + (msg.error || "unknown");
        }
    };
    pc.onicegatheringstatechange = function() {
        if (pc.iceGatheringState === "complete" && pc.localDescription && ws.readyState === 1) {
            ws.send(JSON.stringify({type : "offer", sdp : pc.localDescription}));
        }
    };
    s.peers[id] = rec;
    return id;
});

EM_JS(int, netjs_signaling_join, (const char* urlPtr, const char* codePtr), {
    var url = UTF8ToString(urlPtr);
    var code = UTF8ToString(codePtr);
    if (!Module._netState) Module._netState = {peers : [], nextId : 0};
    var s = Module._netState;
    var id = s.nextId++;
    var pc = new RTCPeerConnection({iceServers : [ {urls : "stun:stun.l.google.com:19302"} ]});
    var rec = {pc : pc, ch : null, role : "client", offerSdp : "", answerSdp : "", ready : false,
               sigCode : code, sigStatus : "connecting"};
    pc.ondatachannel = function(ev) {
        var ch = ev.channel;
        ch.binaryType = "arraybuffer";
        rec.ch = ch;
        ch.onopen = function() { rec.ready = true; rec.sigStatus = "connected"; };
        ch.onclose = function() { rec.ready = false; };
        ch.onmessage = function(mev) {
            if (!Module._netInbox) Module._netInbox = [];
            var data = mev.data;
            if (data instanceof ArrayBuffer) Module._netInbox.push(new Uint8Array(data));
            else if (ArrayBuffer.isView(data)) Module._netInbox.push(new Uint8Array(data.buffer.slice(0)));
        };
    };
    var ws = new WebSocket(url);
    rec.ws = ws;
    ws.onopen = function() {
        rec.sigStatus = "joining room " + code;
        ws.send(JSON.stringify({type : "join", code : code}));
    };
    ws.onerror = function() { rec.sigStatus = "signaling error"; };
    ws.onclose = function() {
        if (!rec.ready && rec.sigStatus !== "connected") rec.sigStatus = "signaling closed";
    };
    ws.onmessage = function(ev) {
        var msg; try { msg = JSON.parse(ev.data); } catch (e) { return; }
        if (msg.type === "joined") {
            rec.sigStatus = "waiting for host offer";
        } else if (msg.type === "offer") {
            rec.sigStatus = "negotiating";
            pc.setRemoteDescription(msg.sdp)
              .then(function() { return pc.createAnswer(); })
              .then(function(answer) { return pc.setLocalDescription(answer); });
        } else if (msg.type === "error") {
            rec.sigStatus = "error: " + (msg.error || "unknown");
        }
    };
    pc.onicegatheringstatechange = function() {
        if (pc.iceGatheringState === "complete" && pc.localDescription && ws.readyState === 1) {
            ws.send(JSON.stringify({type : "answer", sdp : pc.localDescription}));
        }
    };
    s.peers[id] = rec;
    return id;
});

EM_JS(const char*, netjs_signaling_code, (int handle), {
    var s = Module._netState; if (!s) return 0;
    var rec = s.peers[handle]; if (!rec || !rec.sigCode) return 0;
    var bytes = lengthBytesUTF8(rec.sigCode) + 1;
    var ptr = _malloc(bytes);
    stringToUTF8(rec.sigCode, ptr, bytes);
    return ptr;
});

EM_JS(const char*, netjs_signaling_status, (int handle), {
    var s = Module._netState; if (!s) return 0;
    var rec = s.peers[handle]; if (!rec || !rec.sigStatus) return 0;
    var bytes = lengthBytesUTF8(rec.sigStatus) + 1;
    var ptr = _malloc(bytes);
    stringToUTF8(rec.sigStatus, ptr, bytes);
    return ptr;
});
// clang-format on
#endif // __EMSCRIPTEN__

namespace {

// Shortest-angle lerp. Yaw wraps at 360° so a naive a+(b-a)*t would
// spin the long way around when the peer crosses the seam.
float lerpAngleDeg(float a, float b, float t) {
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return a + d * t;
}

} // namespace

void RemotePlayer::pushSample(glm::vec3 pos, float yaw, float pitch, double now) {
    // Shift curr → prev. On the very first sample we have no history;
    // seed prev to the same values so the first interpolated frame is
    // the newcomer's exact pose rather than the zero origin.
    if (currTime == 0.0) {
        prevPos = pos;
        prevYaw = yaw;
        prevPitch = pitch;
        prevTime = now;
    } else {
        prevPos = currPos;
        prevYaw = currYaw;
        prevPitch = currPitch;
        prevTime = currTime;
    }
    currPos = pos;
    currYaw = yaw;
    currPitch = pitch;
    currTime = now;
    lastSeen = now;
}

RemotePlayer::Pose RemotePlayer::sample(double renderTime) const {
    Pose p;
    if (currTime <= prevTime) {
        // Only one distinct sample so far — return it as-is.
        p.pos = currPos;
        p.yaw = currYaw;
        p.pitch = currPitch;
        return p;
    }
    double span = currTime - prevTime;
    double t = (renderTime - prevTime) / span;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0; // clamp; no extrapolation past curr
    float tf = static_cast<float>(t);
    p.pos = glm::mix(prevPos, currPos, tf);
    p.yaw = lerpAngleDeg(prevYaw, currYaw, tf);
    p.pitch = lerpAngleDeg(prevPitch, currPitch, tf);
    return p;
}

NetSession::NetSession() {
#ifdef __EMSCRIPTEN__
    netjs_init_globals();
#endif
}

NetSession::~NetSession() {
    disconnect();
}

bool NetSession::available() const {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

bool NetSession::connected() const {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0) return false;
    return netjs_is_ready(jsHandle_) != 0;
#else
    return false;
#endif
}

std::string NetSession::createOffer() {
#ifdef __EMSCRIPTEN__
    // Idempotent: if we're already hosting, just return whatever the JS
    // side has gathered so far. Tearing down + recreating each frame
    // would restart ICE gathering and never settle.
    if (role_ != NetRole::Host || jsHandle_ < 0) {
        disconnect();
        role_ = NetRole::Host;
        selfPeerId_ = genPeerId();
        jsHandle_ = netjs_create_host();
    }
    return readLocalSdp();
#else
    return "WebRTC unavailable in desktop build";
#endif
}

std::string NetSession::acceptOffer(const std::string& offer) {
#ifdef __EMSCRIPTEN__
    // Idempotent: only create a new peer the first time. Later calls
    // (the menu polling while ICE gathers) just re-read the SDP.
    if (role_ != NetRole::Client || jsHandle_ < 0) {
        disconnect();
        role_ = NetRole::Client;
        selfPeerId_ = genPeerId();
        jsHandle_ = netjs_create_client(offer.c_str());
    }
    return readLocalSdp();
#else
    (void)offer;
    return "WebRTC unavailable in desktop build";
#endif
}

std::string NetSession::readLocalSdp() const {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0) return "";
    const char* sdp = netjs_get_local_sdp(jsHandle_);
    if (!sdp) return "";
    std::string out(sdp);
    std::free((void*)sdp);
    return out;
#else
    return "";
#endif
}

void NetSession::acceptAnswer(const std::string& answer) {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0 || role_ != NetRole::Host) return;
    netjs_accept_answer(jsHandle_, answer.c_str());
#else
    (void)answer;
#endif
}

void NetSession::startHostSignaling(const std::string& signalingUrl) {
#ifdef __EMSCRIPTEN__
    if (role_ == NetRole::Host && jsHandle_ >= 0) return; // idempotent
    disconnect();
    role_ = NetRole::Host;
    selfPeerId_ = genPeerId();
    jsHandle_ = netjs_signaling_host(signalingUrl.c_str());
#else
    (void)signalingUrl;
#endif
}

void NetSession::startJoinSignaling(const std::string& signalingUrl, const std::string& code) {
#ifdef __EMSCRIPTEN__
    if (role_ == NetRole::Client && jsHandle_ >= 0) return;
    disconnect();
    role_ = NetRole::Client;
    selfPeerId_ = genPeerId();
    jsHandle_ = netjs_signaling_join(signalingUrl.c_str(), code.c_str());
#else
    (void)signalingUrl;
    (void)code;
#endif
}

std::string NetSession::signalingCode() const {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0) return "";
    const char* p = netjs_signaling_code(jsHandle_);
    if (!p) return "";
    std::string out(p);
    std::free((void*)p);
    return out;
#else
    return "";
#endif
}

std::string NetSession::signalingStatus() const {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0) return "";
    const char* p = netjs_signaling_status(jsHandle_);
    if (!p) return "";
    std::string out(p);
    std::free((void*)p);
    return out;
#else
    return "";
#endif
}

void NetSession::disconnect() {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ >= 0) {
        // Best-effort graceful close.
        netp::ByeMsg bye{selfPeerId_};
        std::vector<uint8_t> buf;
        netp::encodeBye(buf, bye);
        if (connected()) netjs_send(jsHandle_, buf.data(), (int)buf.size());
        netjs_close(jsHandle_);
    }
#endif
    role_ = NetRole::Off;
    jsHandle_ = -1;
    selfPeerId_ = 0;
    remotes_.clear();
}

void NetSession::sendRaw(const std::vector<uint8_t>& buf) {
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0 || !connected()) return;
    netjs_send(jsHandle_, buf.data(), (int)buf.size());
#else
    (void)buf;
#endif
}

RemotePlayer& NetSession::upsertRemote(uint32_t peerId, double now) {
    for (auto& r : remotes_) {
        if (r.peerId == peerId) {
            r.lastSeen = now;
            return r;
        }
    }
    RemotePlayer r;
    r.peerId = peerId;
    r.lastSeen = now;
    remotes_.push_back(r);
    return remotes_.back();
}

void NetSession::handleMessage(const uint8_t* data, std::size_t len, double now) {
    netp::Op op;
    if (!netp::decodeOp(data, len, op)) return;
    switch (op) {
    case netp::Op::Hello: {
        netp::HelloMsg m;
        if (!netp::decodeHello(data, len, m)) return;
        if (m.protocolVersion != netp::PROTOCOL_VERSION) {
            disconnect();
            return;
        }
        upsertRemote(m.peerId, now);
        // Reply with our own hello so the peer learns our id.
        netp::HelloMsg reply{netp::PROTOCOL_VERSION, selfPeerId_};
        std::vector<uint8_t> buf;
        netp::encodeHello(buf, reply);
        sendRaw(buf);
        // Host greets a new client with an immediate time sync so the
        // sun/clouds snap into place before the first scheduled pulse.
        if (role_ == NetRole::Host) {
            netp::TimeSyncMsg tm{now};
            std::vector<uint8_t> tbuf;
            netp::encodeTimeSync(tbuf, tm);
            sendRaw(tbuf);
            lastTimeSync_ = now;
        }
        break;
    }
    case netp::Op::PlayerState: {
        netp::PlayerStateMsg m;
        if (!netp::decodePlayerState(data, len, m)) return;
        RemotePlayer& r = upsertRemote(m.peerId, now);
        r.pushSample(m.pos, m.yaw, m.pitch, now);
        break;
    }
    case netp::Op::PlaceIntent: {
        if (role_ != NetRole::Host || !world_) return;
        netp::PlaceIntentMsg m;
        if (!netp::decodePlaceIntent(data, len, m)) return;
        world_->placeBlock(m.pos, static_cast<block_type>(m.blockType));
        notifyLocalPlace(m.pos, m.blockType);
        break;
    }
    case netp::Op::DestroyIntent: {
        if (role_ != NetRole::Host || !world_) return;
        netp::DestroyIntentMsg m;
        if (!netp::decodeDestroyIntent(data, len, m)) return;
        world_->destroyBlock(glm::vec3(m.pos));
        notifyLocalDestroy(m.pos);
        break;
    }
    case netp::Op::PlaceApply: {
        if (role_ != NetRole::Client || !world_) return;
        netp::PlaceApplyMsg m;
        if (!netp::decodePlaceApply(data, len, m)) return;
        world_->placeBlock(m.pos, static_cast<block_type>(m.blockType));
        break;
    }
    case netp::Op::DestroyApply: {
        if (role_ != NetRole::Client || !world_) return;
        netp::DestroyApplyMsg m;
        if (!netp::decodeDestroyApply(data, len, m)) return;
        world_->destroyBlock(glm::vec3(m.pos));
        break;
    }
    case netp::Op::Bye: {
        netp::ByeMsg m;
        if (!netp::decodeBye(data, len, m)) return;
        remotes_.erase(std::remove_if(remotes_.begin(), remotes_.end(),
                                      [&](const RemotePlayer& r) { return r.peerId == m.peerId; }),
                       remotes_.end());
        break;
    }
    case netp::Op::TimeSync: {
        // Host is authoritative; ignore if we're hosting.
        if (role_ != NetRole::Client) return;
        netp::TimeSyncMsg m;
        if (!netp::decodeTimeSync(data, len, m)) return;
        // offset = hostClock - clientClock. No RTT compensation — a
        // 10-Hz sun doesn't care about sub-second skew.
        timeOffset_ = m.gameTime - now;
        break;
    }
    }
}

void NetSession::poll(double now) {
    if (role_ == NetRole::Off) return;
#ifdef __EMSCRIPTEN__
    if (jsHandle_ < 0) return;

    // First connect tick: once the channel opens, greet the peer.
    static int lastGreetedHandle = -1;
    static bool lastReady = false;
    bool ready = connected();
    if (ready && (!lastReady || lastGreetedHandle != jsHandle_)) {
        netp::HelloMsg hello{netp::PROTOCOL_VERSION, selfPeerId_};
        std::vector<uint8_t> buf;
        netp::encodeHello(buf, hello);
        sendRaw(buf);
        lastGreetedHandle = jsHandle_;
    }
    lastReady = ready;

    // Drain inbound queue. 4 KB per message is more than enough for our
    // opcodes (largest is PlayerState at 26 bytes).
    constexpr int CAP = 4096;
    static uint8_t scratch[CAP];
    while (netjs_inbox_size() > 0) {
        int n = netjs_inbox_pop(scratch, CAP);
        if (n <= 0) break;
        handleMessage(scratch, static_cast<std::size_t>(n), now);
    }
#endif
    // Time out silent remotes.
    remotes_.erase(std::remove_if(remotes_.begin(), remotes_.end(),
                                  [now](const RemotePlayer& r) { return (now - r.lastSeen) > REMOTE_TIMEOUT; }),
                   remotes_.end());
}

void NetSession::tickBroadcast(const Player& p, double now) {
    if (role_ == NetRole::Off || !connected()) return;
    if ((now - lastBroadcast_) < BROADCAST_INTERVAL) return;
    lastBroadcast_ = now;
    netp::PlayerStateMsg m{};
    m.peerId = selfPeerId_;
    m.pos = p.getPosition();
    m.yaw = p.getYaw();
    m.pitch = p.getPitch();
    std::vector<uint8_t> buf;
    netp::encodePlayerState(buf, m);
    sendRaw(buf);

    // Host emits a clock pulse at ~1 Hz so clients can align the day/night
    // cycle. Once per second is plenty — the sun barely moves in that window
    // and clients only snap offset on receipt, never drift between syncs.
    if (role_ == NetRole::Host && (now - lastTimeSync_) > 1.0) {
        lastTimeSync_ = now;
        netp::TimeSyncMsg tm{now};
        std::vector<uint8_t> tbuf;
        netp::encodeTimeSync(tbuf, tm);
        sendRaw(tbuf);
    }
}

void NetSession::notifyLocalPlace(glm::ivec3 pos, uint8_t blockType) {
    if (!connected()) return;
    std::vector<uint8_t> buf;
    if (role_ == NetRole::Host) {
        netp::PlaceApplyMsg m{pos, blockType};
        netp::encodePlaceApply(buf, m);
    } else {
        netp::PlaceIntentMsg m{pos, blockType};
        netp::encodePlaceIntent(buf, m);
    }
    sendRaw(buf);
}

void NetSession::notifyLocalDestroy(glm::ivec3 pos) {
    if (!connected()) return;
    std::vector<uint8_t> buf;
    if (role_ == NetRole::Host) {
        netp::DestroyApplyMsg m{pos};
        netp::encodeDestroyApply(buf, m);
    } else {
        netp::DestroyIntentMsg m{pos};
        netp::encodeDestroyIntent(buf, m);
    }
    sendRaw(buf);
}
