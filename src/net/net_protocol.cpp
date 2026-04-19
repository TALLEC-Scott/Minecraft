#include "net/net_protocol.h"

#include <cstring>

namespace netp {

namespace {

inline void putU8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

inline void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void putI32LE(std::vector<uint8_t>& out, int32_t v) {
    putU32LE(out, static_cast<uint32_t>(v));
}

inline void putF32LE(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    putU32LE(out, bits);
}

inline void putF64LE(std::vector<uint8_t>& out, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
}

inline uint32_t getU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t getI32LE(const uint8_t* p) {
    return static_cast<int32_t>(getU32LE(p));
}

inline float getF32LE(const uint8_t* p) {
    uint32_t bits = getU32LE(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline double getF64LE(const uint8_t* p) {
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(p[i]) << (i * 8);
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

constexpr std::size_t HELLO_LEN = 1 + 1 + 4;
constexpr std::size_t PLAYER_STATE_LEN = 1 + 4 + 4 * 3 + 4 + 4;
constexpr std::size_t PLACE_INTENT_LEN = 1 + 4 * 3 + 1;
constexpr std::size_t DESTROY_INTENT_LEN = 1 + 4 * 3;
constexpr std::size_t PLACE_APPLY_LEN = 1 + 4 * 3 + 1;
constexpr std::size_t DESTROY_APPLY_LEN = 1 + 4 * 3;
constexpr std::size_t BYE_LEN = 1 + 4;
constexpr std::size_t TIME_SYNC_LEN = 1 + 8;

} // namespace

// --- Encoders ---

void encodeHello(std::vector<uint8_t>& out, const HelloMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::Hello));
    putU8(out, m.protocolVersion);
    putU32LE(out, m.peerId);
}

void encodePlayerState(std::vector<uint8_t>& out, const PlayerStateMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::PlayerState));
    putU32LE(out, m.peerId);
    putF32LE(out, m.pos.x);
    putF32LE(out, m.pos.y);
    putF32LE(out, m.pos.z);
    putF32LE(out, m.yaw);
    putF32LE(out, m.pitch);
}

void encodePlaceIntent(std::vector<uint8_t>& out, const PlaceIntentMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::PlaceIntent));
    putI32LE(out, m.pos.x);
    putI32LE(out, m.pos.y);
    putI32LE(out, m.pos.z);
    putU8(out, m.blockType);
}

void encodeDestroyIntent(std::vector<uint8_t>& out, const DestroyIntentMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::DestroyIntent));
    putI32LE(out, m.pos.x);
    putI32LE(out, m.pos.y);
    putI32LE(out, m.pos.z);
}

void encodePlaceApply(std::vector<uint8_t>& out, const PlaceApplyMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::PlaceApply));
    putI32LE(out, m.pos.x);
    putI32LE(out, m.pos.y);
    putI32LE(out, m.pos.z);
    putU8(out, m.blockType);
}

void encodeDestroyApply(std::vector<uint8_t>& out, const DestroyApplyMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::DestroyApply));
    putI32LE(out, m.pos.x);
    putI32LE(out, m.pos.y);
    putI32LE(out, m.pos.z);
}

void encodeBye(std::vector<uint8_t>& out, const ByeMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::Bye));
    putU32LE(out, m.peerId);
}

void encodeTimeSync(std::vector<uint8_t>& out, const TimeSyncMsg& m) {
    putU8(out, static_cast<uint8_t>(Op::TimeSync));
    putF64LE(out, m.gameTime);
}

// --- Decoders ---

bool decodeOp(const uint8_t* buf, std::size_t len, Op& outOp) {
    if (len < 1) return false;
    outOp = static_cast<Op>(buf[0]);
    return true;
}

bool decodeHello(const uint8_t* buf, std::size_t len, HelloMsg& out) {
    if (len != HELLO_LEN || buf[0] != static_cast<uint8_t>(Op::Hello)) return false;
    out.protocolVersion = buf[1];
    out.peerId = getU32LE(buf + 2);
    return true;
}

bool decodePlayerState(const uint8_t* buf, std::size_t len, PlayerStateMsg& out) {
    if (len != PLAYER_STATE_LEN || buf[0] != static_cast<uint8_t>(Op::PlayerState)) return false;
    out.peerId = getU32LE(buf + 1);
    out.pos.x = getF32LE(buf + 5);
    out.pos.y = getF32LE(buf + 9);
    out.pos.z = getF32LE(buf + 13);
    out.yaw = getF32LE(buf + 17);
    out.pitch = getF32LE(buf + 21);
    return true;
}

bool decodePlaceIntent(const uint8_t* buf, std::size_t len, PlaceIntentMsg& out) {
    if (len != PLACE_INTENT_LEN || buf[0] != static_cast<uint8_t>(Op::PlaceIntent)) return false;
    out.pos.x = getI32LE(buf + 1);
    out.pos.y = getI32LE(buf + 5);
    out.pos.z = getI32LE(buf + 9);
    out.blockType = buf[13];
    return true;
}

bool decodeDestroyIntent(const uint8_t* buf, std::size_t len, DestroyIntentMsg& out) {
    if (len != DESTROY_INTENT_LEN || buf[0] != static_cast<uint8_t>(Op::DestroyIntent)) return false;
    out.pos.x = getI32LE(buf + 1);
    out.pos.y = getI32LE(buf + 5);
    out.pos.z = getI32LE(buf + 9);
    return true;
}

bool decodePlaceApply(const uint8_t* buf, std::size_t len, PlaceApplyMsg& out) {
    if (len != PLACE_APPLY_LEN || buf[0] != static_cast<uint8_t>(Op::PlaceApply)) return false;
    out.pos.x = getI32LE(buf + 1);
    out.pos.y = getI32LE(buf + 5);
    out.pos.z = getI32LE(buf + 9);
    out.blockType = buf[13];
    return true;
}

bool decodeDestroyApply(const uint8_t* buf, std::size_t len, DestroyApplyMsg& out) {
    if (len != DESTROY_APPLY_LEN || buf[0] != static_cast<uint8_t>(Op::DestroyApply)) return false;
    out.pos.x = getI32LE(buf + 1);
    out.pos.y = getI32LE(buf + 5);
    out.pos.z = getI32LE(buf + 9);
    return true;
}

bool decodeBye(const uint8_t* buf, std::size_t len, ByeMsg& out) {
    if (len != BYE_LEN || buf[0] != static_cast<uint8_t>(Op::Bye)) return false;
    out.peerId = getU32LE(buf + 1);
    return true;
}

bool decodeTimeSync(const uint8_t* buf, std::size_t len, TimeSyncMsg& out) {
    if (len != TIME_SYNC_LEN || buf[0] != static_cast<uint8_t>(Op::TimeSync)) return false;
    out.gameTime = getF64LE(buf + 1);
    return true;
}

} // namespace netp
