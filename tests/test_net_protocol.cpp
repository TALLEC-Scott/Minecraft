#include <gtest/gtest.h>

#include "net/net_protocol.h"

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
