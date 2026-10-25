#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "chat_commands.h"
#include "day_cycle.h"

namespace {

bool replyContains(const ChatCommands::Reply& reply, const std::string& needle) {
    for (const std::string& line : reply)
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(ChatCommandsTest, OnlySlashLinesAreCommands) {
    EXPECT_TRUE(ChatCommands::isCommand("/help"));
    EXPECT_FALSE(ChatCommands::isCommand("hello /help"));
    EXPECT_FALSE(ChatCommands::isCommand(""));
}

TEST(ChatCommandsTest, DispatchesWithArgsCaseInsensitively) {
    ChatCommands cmds;
    ChatCommands::Args seen;
    cmds.add("time", "<day|night>", "Set the time", [&](const ChatCommands::Args& args, ChatCommands::Reply& out) {
        seen = args;
        out.emplace_back("ok");
    });
    ChatCommands::Reply r = cmds.run("/TIME  set   day");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], "ok");
    EXPECT_EQ(seen, (ChatCommands::Args{"set", "day"}));
}

TEST(ChatCommandsTest, UnknownCommandPointsToHelp) {
    ChatCommands cmds;
    ChatCommands::Reply r = cmds.run("/nope");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NE(r[0].find("/nope"), std::string::npos);
    EXPECT_NE(r[0].find("/help"), std::string::npos);
}

TEST(ChatCommandsTest, HelpListsEveryCommand) {
    ChatCommands cmds;
    cmds.add("time", "<day|night>", "Set the time", [](const ChatCommands::Args&, ChatCommands::Reply&) {});
    cmds.add("cowrain", "[on|off]", "Cows", [](const ChatCommands::Args&, ChatCommands::Reply&) {});
    ChatCommands::Reply r = cmds.run("/help");
    EXPECT_TRUE(replyContains(r, "/help"));
    EXPECT_TRUE(replyContains(r, "/time <day|night> - Set the time"));
    EXPECT_TRUE(replyContains(r, "/cowrain [on|off] - Cows"));

    ChatCommands::Reply one = cmds.run("/help /time");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "/time <day|night> - Set the time");
}

TEST(DayCycleTest, SecondsUntilLandsOnTheTargetAngle) {
    for (double clock : {0.0, 10.0, 62.8, 123.4, 1000.0}) {
        for (double target : {daycycle::NOON, daycycle::MIDNIGHT}) {
            double dt = daycycle::secondsUntil(clock, target);
            EXPECT_GE(dt, 0.0);
            EXPECT_LT(dt, glm::two_pi<double>() / daycycle::RADIANS_PER_SECOND);
            double angle = (clock + dt) * daycycle::RADIANS_PER_SECOND;
            EXPECT_NEAR(std::sin(angle), std::sin(target), 1e-9) << "clock " << clock;
            EXPECT_NEAR(std::cos(angle), std::cos(target), 1e-9) << "clock " << clock;
        }
    }
}

TEST(DayCycleTest, NoonIsSunHighAndMidnightSunLow) {
    EXPECT_NEAR(std::sin(daycycle::NOON), 1.0, 1e-12);
    EXPECT_NEAR(std::sin(daycycle::MIDNIGHT), -1.0, 1e-12);
}

TEST(ChatCommandsTest, ParseToggle) {
    bool out = false;
    ASSERT_TRUE(ChatCommands::parseToggle("", false, out));
    EXPECT_TRUE(out) << "no argument flips";
    ASSERT_TRUE(ChatCommands::parseToggle("", true, out));
    EXPECT_FALSE(out);
    ASSERT_TRUE(ChatCommands::parseToggle("ON", false, out));
    EXPECT_TRUE(out);
    ASSERT_TRUE(ChatCommands::parseToggle("true", false, out));
    EXPECT_TRUE(out);
    ASSERT_TRUE(ChatCommands::parseToggle("off", true, out));
    EXPECT_FALSE(out);
    ASSERT_TRUE(ChatCommands::parseToggle("false", true, out));
    EXPECT_FALSE(out);
    EXPECT_FALSE(ChatCommands::parseToggle("maybe", false, out));
}
