#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "cube.h"

class Player;
class World;

enum class NetRole { Off, Host, Client };

struct RemotePlayer {
    uint32_t peerId = 0;
    // Double-buffered snapshots. `curr` is the most recent sample
    // received, `prev` is the one before it. Render-time queries lerp
    // between the two so remotes glide at the frame rate rather than
    // snapping at the network rate (10 Hz).
    glm::vec3 prevPos{0.0f};
    glm::vec3 currPos{0.0f};
    float prevYaw = 0.0f, currYaw = 0.0f;
    float prevPitch = 0.0f, currPitch = 0.0f;
    // Local clock times (glfwGetTime()) at which each snapshot was
    // received. lastSeen mirrors currTime but is kept separately so peer
    // timeout logic is unaffected if we ever change the render delay.
    double prevTime = 0.0;
    double currTime = 0.0;
    double lastSeen = 0.0;

    struct Pose {
        glm::vec3 pos{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    // Ingest a freshly decoded PlayerState. Shifts curr → prev and
    // records the new sample at `now`.
    void pushSample(glm::vec3 pos, float yaw, float pitch, double now);
    // Interpolated pose at `renderTime` — which should be slightly in
    // the past (see RENDER_DELAY) so we always have a `curr` ahead of
    // it. Falls back to the latest sample if only one is available or
    // `renderTime` exceeds it.
    Pose sample(double renderTime) const;
};

// Render one network interval behind the latest sample so lerp always
// has a valid pair. 10 Hz updates → 100 ms; a bit of slack swallows
// network jitter without turning into perceptible lag.
inline constexpr double RENDER_DELAY = 0.12;

// MVP WebRTC session. Web builds (Emscripten) wire this up to a real
// RTCPeerConnection + DataChannel; desktop builds compile a stub whose
// connected() stays false and whose send paths are no-ops. Signaling is
// manual: the host generates an SDP offer (copy-paste), the client pastes
// it back and returns an answer, and the host pastes that answer in.
class NetSession {
  public:
    NetSession();
    ~NetSession();

    // Accessors.
    NetRole role() const { return role_; }
    bool connected() const;
    bool available() const; // true only on web builds
    uint32_t selfPeerId() const { return selfPeerId_; }

    // Signaling. Called from the Multiplayer menu; stubs return a static
    // error string on desktop so the user sees "unavailable".
    // `createOffer()` transitions role to Host. Idempotent — calling it
    // again with a session already live just re-reads the current local
    // SDP (which may still be empty while ICE gathers).
    std::string createOffer();
    // `acceptOffer` transitions role to Client and returns the answer SDP
    // to copy back to the host. Idempotent — calling again with the same
    // pasted offer re-reads the current local SDP.
    std::string acceptOffer(const std::string& offer);
    // Read the current local SDP (offer for host, answer for client). May
    // be empty until ICE gathering completes. Non-destructive.
    std::string readLocalSdp() const;
    // Called by the host once the peer's answer SDP is pasted.
    void acceptAnswer(const std::string& answer);
    void disconnect();

    // One-click signaling-server flow — the client opens a WebSocket to
    // `signalingUrl`, the server pairs two peers by room code, and SDPs
    // are swapped automatically. `startHostSignaling` produces a code
    // (read via `signalingCode`), `startJoinSignaling` consumes one.
    // Both return immediately; the UI polls `signalingStatus()` until
    // `connected()` flips true.
    void startHostSignaling(const std::string& signalingUrl);
    void startJoinSignaling(const std::string& signalingUrl, const std::string& code);
    std::string signalingCode() const;
    std::string signalingStatus() const;

    // Runtime pump. `poll()` drains incoming messages and applies them to
    // the bound world / remote-player list. `tickBroadcast()` emits a
    // PlayerState packet at ~10 Hz.
    void poll(double now);
    void tickBroadcast(const Player& p, double now);

    // Called by Player when the local user initiates a block edit. The
    // session decides what goes on the wire based on role:
    //  - Host:   sends PLACE_APPLY / DESTROY_APPLY (authoritative echo).
    //  - Client: sends PLACE_INTENT / DESTROY_INTENT (asks host to apply).
    //  - Off:    no-op.
    void notifyLocalPlace(glm::ivec3 pos, uint8_t blockType);
    void notifyLocalDestroy(glm::ivec3 pos);

    // Shared game clock. On the host this is just glfwGetTime(); on a
    // connected client it's glfwGetTime() plus the offset learned from
    // the host's most-recent TimeSync. Everything that should run in
    // lockstep across peers (sun position, etc.) should read this rather
    // than calling glfwGetTime() directly.
    double syncedTime(double localNow) const {
        return (role_ == NetRole::Client) ? localNow + timeOffset_ : localNow;
    }

    // Bind the world so incoming APPLY packets can reach it.
    void bindWorld(World* w) { world_ = w; }

    // Chat. Both roles may send and receive freely over the same data
    // channel. `sendChat` no-ops when disconnected so callers don't need
    // to gate on connected().
    void sendChat(const std::string& text);
    void setOnChat(std::function<void(uint32_t peerId, const std::string& text)> cb) {
        onChat_ = std::move(cb);
    }

    const std::vector<RemotePlayer>& remotes() const { return remotes_; }

  private:
    NetRole role_ = NetRole::Off;
    uint32_t selfPeerId_ = 0;
    double lastBroadcast_ = 0.0;
    double lastTimeSync_ = 0.0;
    // offset = host's syncedTime - our local glfwGetTime(); zero until the
    // first TimeSync arrives.
    double timeOffset_ = 0.0;
    std::vector<RemotePlayer> remotes_;
    World* world_ = nullptr;
    std::function<void(uint32_t, const std::string&)> onChat_;

    // Web-only: handle into the JS side's RTCPeerConnection registry.
    // Unused on desktop builds (kept to keep the struct layout identical
    // across platforms so tests that never touch the session still link).
    int jsHandle_ = -1;

    // Dispatch for a decoded inbound message.
    void handleMessage(const uint8_t* data, std::size_t len, double now);
    // Actually push bytes out to the peer.
    void sendRaw(const std::vector<uint8_t>& buf);

    // Touch/insert the remote-player record for `peerId`.
    RemotePlayer& upsertRemote(uint32_t peerId, double now);
};
