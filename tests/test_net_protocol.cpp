#include <gtest/gtest.h>

#include <limits>

#include "net/net_protocol.h"
#include "net/net_session.h"

using namespace netp;

TEST(NetProtocol, HelloRoundtrip) {
    HelloMsg in{PROTOCOL_VERSION, 0xDEADBEEFu};
    std::vector<uint8_t> buf;
    encodeHello(buf, in);
    Op op;
    ASSERT_TRUE(decodeOp(buf.data(), buf.size(), op));
    EXPECT_EQ(op, Op::Hello);
    HelloMsg out{};
    ASSERT_TRUE(decodeHello(buf.data(), buf.size(), out));
    EXPECT_EQ(out.protocolVersion, in.protocolVersion);
    EXPECT_EQ(out.peerId, in.peerId);
}

TEST(NetProtocol, PlayerStateRoundtrip) {
    PlayerStateMsg in{};
    in.peerId = 0x01020304u;
    in.pos = glm::vec3(123.5f, -7.25f, 1024.0f);
    in.yaw = -45.75f;
    in.pitch = 30.125f;
    in.heldItem = 256; // iron sword
    in.flags = PLAYER_FLAG_DEAD;

    std::vector<uint8_t> buf;
    encodePlayerState(buf, in);
    Op op;
    ASSERT_TRUE(decodeOp(buf.data(), buf.size(), op));
    EXPECT_EQ(op, Op::PlayerState);
    PlayerStateMsg out{};
    ASSERT_TRUE(decodePlayerState(buf.data(), buf.size(), out));
    EXPECT_EQ(out.peerId, in.peerId);
    EXPECT_FLOAT_EQ(out.pos.x, in.pos.x);
    EXPECT_FLOAT_EQ(out.pos.y, in.pos.y);
    EXPECT_FLOAT_EQ(out.pos.z, in.pos.z);
    EXPECT_FLOAT_EQ(out.yaw, in.yaw);
    EXPECT_FLOAT_EQ(out.pitch, in.pitch);
    EXPECT_EQ(out.heldItem, in.heldItem);
    EXPECT_EQ(out.flags, in.flags);
}

TEST(NetProtocol, PlaceIntentRoundtrip) {
    PlaceIntentMsg in{glm::ivec3(-5, 64, 7), 3};
    std::vector<uint8_t> buf;
    encodePlaceIntent(buf, in);
    PlaceIntentMsg out{};
    ASSERT_TRUE(decodePlaceIntent(buf.data(), buf.size(), out));
    EXPECT_EQ(out.pos, in.pos);
    EXPECT_EQ(out.blockType, in.blockType);
}

TEST(NetProtocol, DestroyIntentRoundtrip) {
    DestroyIntentMsg in{glm::ivec3(100000, 2, -99999)};
    std::vector<uint8_t> buf;
    encodeDestroyIntent(buf, in);
    DestroyIntentMsg out{};
    ASSERT_TRUE(decodeDestroyIntent(buf.data(), buf.size(), out));
    EXPECT_EQ(out.pos, in.pos);
}

TEST(NetProtocol, PlaceApplyRoundtrip) {
    PlaceApplyMsg in{glm::ivec3(1, 2, 3), 255};
    std::vector<uint8_t> buf;
    encodePlaceApply(buf, in);
    PlaceApplyMsg out{};
    ASSERT_TRUE(decodePlaceApply(buf.data(), buf.size(), out));
    EXPECT_EQ(out.pos, in.pos);
    EXPECT_EQ(out.blockType, in.blockType);
}

TEST(NetProtocol, DestroyApplyRoundtrip) {
    DestroyApplyMsg in{glm::ivec3(-1, -2, -3)};
    std::vector<uint8_t> buf;
    encodeDestroyApply(buf, in);
    DestroyApplyMsg out{};
    ASSERT_TRUE(decodeDestroyApply(buf.data(), buf.size(), out));
    EXPECT_EQ(out.pos, in.pos);
}

TEST(NetProtocol, ByeRoundtrip) {
    ByeMsg in{42u};
    std::vector<uint8_t> buf;
    encodeBye(buf, in);
    ByeMsg out{};
    ASSERT_TRUE(decodeBye(buf.data(), buf.size(), out));
    EXPECT_EQ(out.peerId, in.peerId);
}

TEST(NetProtocol, TimeSyncRoundtrip) {
    TimeSyncMsg in{1234567.89012345};
    std::vector<uint8_t> buf;
    encodeTimeSync(buf, in);
    TimeSyncMsg out{};
    ASSERT_TRUE(decodeTimeSync(buf.data(), buf.size(), out));
    EXPECT_DOUBLE_EQ(out.gameTime, in.gameTime);
}

TEST(NetProtocol, TruncatedPayloadRejected) {
    // An empty buffer must not yield an opcode.
    Op op;
    EXPECT_FALSE(decodeOp(nullptr, 0, op));

    // Well-formed hello truncated by one byte must fail to decode.
    HelloMsg hm{PROTOCOL_VERSION, 7u};
    std::vector<uint8_t> buf;
    encodeHello(buf, hm);
    buf.pop_back();
    HelloMsg out{};
    EXPECT_FALSE(decodeHello(buf.data(), buf.size(), out));
}

TEST(NetProtocol, WrongOpcodeRejected) {
    // A HelloMsg-length buffer starting with a different opcode must fail.
    HelloMsg hm{PROTOCOL_VERSION, 0u};
    std::vector<uint8_t> buf;
    encodeHello(buf, hm);
    buf[0] = static_cast<uint8_t>(Op::Bye);
    HelloMsg out{};
    EXPECT_FALSE(decodeHello(buf.data(), buf.size(), out));
}

TEST(NetProtocol, ChatRoundtrip) {
    ChatMsg in{0xABCD1234u, "hello world"};
    std::vector<uint8_t> buf;
    encodeChat(buf, in);
    Op op;
    ASSERT_TRUE(decodeOp(buf.data(), buf.size(), op));
    EXPECT_EQ(op, Op::Chat);
    ChatMsg out{};
    ASSERT_TRUE(decodeChat(buf.data(), buf.size(), out));
    EXPECT_EQ(out.peerId, in.peerId);
    EXPECT_EQ(out.text, in.text);
}

TEST(NetProtocol, ChatEmptyRoundtrip) {
    ChatMsg in{7u, ""};
    std::vector<uint8_t> buf;
    encodeChat(buf, in);
    // 1 byte opcode + 4 byte peerId + 2 byte length + 0 text.
    EXPECT_EQ(buf.size(), 1u + 4u + 2u);
    ChatMsg out{};
    ASSERT_TRUE(decodeChat(buf.data(), buf.size(), out));
    EXPECT_EQ(out.peerId, in.peerId);
    EXPECT_TRUE(out.text.empty());
}

TEST(NetProtocol, ChatTruncatedRejected) {
    // Encoding a message and lopping off the last byte should fail the
    // "len == header + textLen" guard in decodeChat.
    ChatMsg in{1u, "abcdefgh"};
    std::vector<uint8_t> buf;
    encodeChat(buf, in);
    buf.pop_back();
    ChatMsg out{};
    EXPECT_FALSE(decodeChat(buf.data(), buf.size(), out));

    // Flipping the length prefix so it claims more bytes than the buffer
    // provides must also fail.
    buf.clear();
    encodeChat(buf, in);
    buf[5] = 0xFF; // low byte of textLen
    buf[6] = 0xFF; // high byte of textLen
    EXPECT_FALSE(decodeChat(buf.data(), buf.size(), out));
}

TEST(NetProtocol, ChatOversizedLengthPrefixRejected) {
    // Hand-craft a header whose textLen exceeds CHAT_MAX_TEXT_LEN. The
    // buffer is allocated at full size up front: growing it with resize()
    // after the header trips a GCC 13 -Warray-bounds false positive.
    uint16_t overCap = static_cast<uint16_t>(CHAT_MAX_TEXT_LEN + 1);
    std::vector<uint8_t> buf(7 + overCap, 'x');
    buf[0] = static_cast<uint8_t>(Op::Chat);
    buf[1] = 0x01; // peerId
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = static_cast<uint8_t>(overCap & 0xFF); // textLen
    buf[6] = static_cast<uint8_t>((overCap >> 8) & 0xFF);
    ChatMsg out{};
    EXPECT_FALSE(decodeChat(buf.data(), buf.size(), out));
}

TEST(NetProtocol, HitRoundtrip) {
    HitMsg in{0x11112222u, 0x33334444u, 9.0f, {1.5f, 70.25f, -3.0f}};
    std::vector<uint8_t> buf;
    encodeHit(buf, in);
    EXPECT_EQ(buf.size(), 25u);
    Op op;
    ASSERT_TRUE(decodeOp(buf.data(), buf.size(), op));
    EXPECT_EQ(op, Op::Hit);
    EXPECT_GE(static_cast<int>(Op::Hit), 0x20) << "new features claim opcodes 0x20+";
    HitMsg out{};
    ASSERT_TRUE(decodeHit(buf.data(), buf.size(), out));
    EXPECT_EQ(out.attackerId, in.attackerId);
    EXPECT_EQ(out.targetId, in.targetId);
    EXPECT_FLOAT_EQ(out.damage, 9.0f);
    EXPECT_FLOAT_EQ(out.fromPos.y, 70.25f);
}

TEST(NetProtocol, HitRejectsTruncatedAndBogusDamage) {
    std::vector<uint8_t> buf;
    encodeHit(buf, HitMsg{1, 2, 6.0f, {0, 0, 0}});
    HitMsg out{};
    EXPECT_FALSE(decodeHit(buf.data(), buf.size() - 1, out));
    for (float bad : {-1.0f, HIT_MAX_DAMAGE + 1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        std::vector<uint8_t> b;
        encodeHit(b, HitMsg{1, 2, bad, {0, 0, 0}});
        EXPECT_FALSE(decodeHit(b.data(), b.size(), out)) << bad;
    }
}

TEST(NetPvp, HitsLandOutsideTheTargetsInvulnerabilityOnly) {
    RemotePlayer r;
    EXPECT_TRUE(pvpHitWouldLand(r, 10.0)) << "never hit before";
    r.lastHitLanded = 10.0;
    EXPECT_FALSE(pvpHitWouldLand(r, 10.25)) << "fist spam inside the 0.5 s window";
    EXPECT_TRUE(pvpHitWouldLand(r, 10.0 + PVP_INVULNERABLE_SECONDS));
    r.dead = true;
    EXPECT_FALSE(pvpHitWouldLand(r, 20.0)) << "dead players can't be hit";
}
