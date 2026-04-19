#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace netp {

constexpr uint8_t PROTOCOL_VERSION = 1;

// Wire opcodes. Low bytes (<0x20) are reserved for this MVP protocol so
// later additions (chat, entity sync) can claim 0x20+ without renumbering.
enum class Op : uint8_t {
    Hello = 0x01,
    PlayerState = 0x02,
    PlaceIntent = 0x03,
    DestroyIntent = 0x04,
    PlaceApply = 0x05,
    DestroyApply = 0x06,
    Bye = 0x07,
    // Host → client authoritative game-clock broadcast. Clients snap an
    // internal offset to it so the day/night cycle stays in lockstep.
    TimeSync = 0x08,
};

struct HelloMsg {
    uint8_t protocolVersion = PROTOCOL_VERSION;
    uint32_t peerId = 0;
};

struct PlayerStateMsg {
    uint32_t peerId = 0;
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct PlaceIntentMsg {
    glm::ivec3 pos{0};
    uint8_t blockType = 0;
};

struct DestroyIntentMsg {
    glm::ivec3 pos{0};
};

struct PlaceApplyMsg {
    glm::ivec3 pos{0};
    uint8_t blockType = 0;
};

struct DestroyApplyMsg {
    glm::ivec3 pos{0};
};

struct ByeMsg {
    uint32_t peerId = 0;
};

// Host's game clock in seconds (same scale as glfwGetTime()). Sent
// periodically by the host; clients compute offset = gameTime - local.
struct TimeSyncMsg {
    double gameTime = 0.0;
};

// Encoders append a little-endian framed message (opcode + payload) to `out`.
void encodeHello(std::vector<uint8_t>& out, const HelloMsg& m);
void encodePlayerState(std::vector<uint8_t>& out, const PlayerStateMsg& m);
void encodePlaceIntent(std::vector<uint8_t>& out, const PlaceIntentMsg& m);
void encodeDestroyIntent(std::vector<uint8_t>& out, const DestroyIntentMsg& m);
void encodePlaceApply(std::vector<uint8_t>& out, const PlaceApplyMsg& m);
void encodeDestroyApply(std::vector<uint8_t>& out, const DestroyApplyMsg& m);
void encodeBye(std::vector<uint8_t>& out, const ByeMsg& m);
void encodeTimeSync(std::vector<uint8_t>& out, const TimeSyncMsg& m);

// Decoders: read opcode at buf[0], verify length matches the payload for
// that opcode, and fill `out`. Return true on success, false on short /
// truncated / wrong-opcode input.
bool decodeOp(const uint8_t* buf, std::size_t len, Op& outOp);
bool decodeHello(const uint8_t* buf, std::size_t len, HelloMsg& out);
bool decodePlayerState(const uint8_t* buf, std::size_t len, PlayerStateMsg& out);
bool decodePlaceIntent(const uint8_t* buf, std::size_t len, PlaceIntentMsg& out);
bool decodeDestroyIntent(const uint8_t* buf, std::size_t len, DestroyIntentMsg& out);
bool decodePlaceApply(const uint8_t* buf, std::size_t len, PlaceApplyMsg& out);
bool decodeDestroyApply(const uint8_t* buf, std::size_t len, DestroyApplyMsg& out);
bool decodeBye(const uint8_t* buf, std::size_t len, ByeMsg& out);
bool decodeTimeSync(const uint8_t* buf, std::size_t len, TimeSyncMsg& out);

} // namespace netp
