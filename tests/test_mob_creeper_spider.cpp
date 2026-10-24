#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "mob_spawn.h"
#include "mob_test_support.h"
#include "mobs/creeper.h"
#include "mobs/spider.h"

namespace {

// Harness with a targetable player and counting explode / damage hooks.
struct HostileHarness : MobHarness {
    int explosions = 0;
    glm::vec3 lastExplosion{0.0f};
    float lastPower = 0.0f;
    int playerHits = 0;

    HostileHarness() {
        floor();
        ctx.onExplode = [this](glm::vec3 c, float power) {
            ++explosions;
            lastExplosion = c;
            lastPower = power;
        };
        ctx.onDamagePlayer = [this](float /*amount*/, glm::vec3 /*from*/) { ++playerHits; };
    }
    void placePlayer(glm::vec3 feet) {
        ctx.playerTargetable = true;
        ctx.playerFeet = feet;
        ctx.playerEye = feet + glm::vec3(0.0f, 1.62f, 0.0f);
    }
    void night() { ctx.skyDarken = 11; }
};

template <typename T> void place(T& m, glm::vec3 feet) {
    m.pos = m.prevPos = feet;
}

} // namespace

// --- Registry ----------------------------------------------------------------

TEST(CreeperSpiderTest, RegistryRows) {
    const MobInfo& c = mobInfo(MobType::Creeper);
    EXPECT_STREQ(c.name, "creeper");
    EXPECT_EQ(c.category, MobCategory::Hostile);
    EXPECT_FLOAT_EQ(c.halfWidth, 0.3f);
    EXPECT_FLOAT_EQ(c.height, 1.7f);
    EXPECT_FLOAT_EQ(c.maxHealth, 20.0f);
    EXPECT_EQ(c.spawnBiomes, ALL_LAND_BIOMES);
    EXPECT_EQ(c.groupMin, 1);
    EXPECT_EQ(c.groupMax, 4);
    EXPECT_EQ(c.spawnWeight, 100);

    const MobInfo& s = mobInfo(MobType::Spider);
    EXPECT_STREQ(s.name, "spider");
    EXPECT_EQ(s.category, MobCategory::Hostile);
    EXPECT_FLOAT_EQ(s.halfWidth, 0.7f);
    EXPECT_FLOAT_EQ(s.height, 0.9f);
    EXPECT_FLOAT_EQ(s.maxHealth, 16.0f);
    EXPECT_EQ(s.spawnBiomes, ALL_LAND_BIOMES);
    EXPECT_EQ(s.groupMin, 1);
    EXPECT_EQ(s.groupMax, 4);
    EXPECT_EQ(s.spawnWeight, 100);

    auto creeper = createMob(MobType::Creeper);
    auto spider = createMob(MobType::Spider);
    EXPECT_EQ(creeper->type, MobType::Creeper);
    EXPECT_EQ(spider->type, MobType::Spider);
    EXPECT_FLOAT_EQ(spider->health, 16.0f);
    EXPECT_LE(creeper->model().size(), MAX_MOB_PARTS);
    EXPECT_LE(spider->model().size(), MAX_MOB_PARTS);

    // Spider eyes glow in the dark (fullbright layer); nothing else does.
    int eyes = 0;
    for (const MobPart& p : spider->model().parts) {
        bool isEye = std::string(p.name) == "eye";
        EXPECT_EQ(p.emissive, isEye) << p.name;
        eyes += isEye ? 1 : 0;
    }
    EXPECT_EQ(eyes, 8);
    for (const MobPart& p : creeper->model().parts)
        EXPECT_FALSE(p.emissive) << p.name;

    std::vector<MobType> hostile = mobspawn::eligibleTypes(MobCategory::Hostile, BIOME_DESERT);
    EXPECT_NE(std::find(hostile.begin(), hostile.end(), MobType::Creeper), hostile.end());
    EXPECT_NE(std::find(hostile.begin(), hostile.end(), MobType::Spider), hostile.end());
    std::vector<MobType> ocean = mobspawn::eligibleTypes(MobCategory::Hostile, BIOME_OCEAN);
    EXPECT_EQ(std::find(ocean.begin(), ocean.end(), MobType::Creeper), ocean.end());
}

// --- Creeper -----------------------------------------------------------------

TEST(CreeperSpiderTest, CreeperSwellsNearPlayerAndExplodesAfterFuse) {
    HostileHarness h;
    Creeper c;
    place(c, glm::vec3(0, 0.5f, 0));
    h.placePlayer(glm::vec3(2.0f, 0.5f, 0));

    h.run(c, 1);
    EXPECT_EQ(c.swellTicks(), 1);
    EXPECT_TRUE(c.pendingSounds & (1u << static_cast<unsigned>(MobSoundEvent::Attack))) << "hiss on ignition";

    h.run(c, Creeper::FUSE_TICKS - 2);
    EXPECT_EQ(h.explosions, 0);
    EXPECT_FALSE(c.removed);
    MobPose p = c.pose(1.0f);
    EXPECT_GT(p.scale.x, 1.2f) << "swollen sideways near the end of the fuse";
    EXPECT_GT(p.scale.x, p.scale.y);

    h.run(c, 1);
    EXPECT_EQ(h.explosions, 1);
    EXPECT_FLOAT_EQ(h.lastPower, Creeper::EXPLOSION_POWER);
    EXPECT_NEAR(h.lastExplosion.y, c.center().y, 1e-4f);
    EXPECT_TRUE(c.removed);
    EXPECT_FALSE(c.dying) << "removed outright, no death animation";
    EXPECT_FLOAT_EQ(c.health, 20.0f);

    h.run(c, 5);
    EXPECT_EQ(h.explosions, 1) << "explodes exactly once";
}

TEST(CreeperSpiderTest, CreeperWhiteFlashPulses) {
    HostileHarness h;
    Creeper c;
    place(c, glm::vec3(0, 0.5f, 0));
    h.placePlayer(glm::vec3(1.5f, 0.5f, 0));
    bool sawFlash = false, sawDark = false;
    for (int i = 0; i < Creeper::FUSE_TICKS - 1; ++i) {
        h.run(c, 1);
        float w = c.pose(1.0f).whiteFlash;
        if (c.swelling(1.0f) > 0.15f) (w > 0.0f ? sawFlash : sawDark) = true;
    }
    EXPECT_TRUE(sawFlash);
    EXPECT_TRUE(sawDark);
}

TEST(CreeperSpiderTest, CreeperDeflatesWhenPlayerLeaves) {
    HostileHarness h;
    Creeper c;
    place(c, glm::vec3(0, 0.5f, 0));
    h.placePlayer(glm::vec3(2.0f, 0.5f, 0));
    h.run(c, 10);
    ASSERT_EQ(c.swellTicks(), 10);

    h.placePlayer(glm::vec3(12.0f, 0.5f, 0)); // beyond the 7-block defuse range
    h.run(c, 15);
    EXPECT_EQ(c.swellTicks(), 0);
    EXPECT_EQ(h.explosions, 0);
    EXPECT_FALSE(c.removed);
    EXPECT_FLOAT_EQ(c.pose(1.0f).scale.x, 1.0f);
    EXPECT_GT(c.pos.x, 0.5f) << "resumes walking toward the player";
}

TEST(CreeperSpiderTest, CreeperIgnoresDistantPlayerAndBlockedSight) {
    HostileHarness h;
    Creeper c;
    place(c, glm::vec3(0, 0.5f, 0));
    // Within 3 blocks but behind a wall: the goal starts yet the fuse never
    // advances (Minecraft needs line of sight).
    h.blocks.fill(1, 1, -3, 1, 4, 3, STONE);
    h.placePlayer(glm::vec3(2.4f, 0.5f, 0));
    h.run(c, 40);
    EXPECT_EQ(c.swellTicks(), 0);
    EXPECT_EQ(h.explosions, 0);
}

// --- Spider ------------------------------------------------------------------

TEST(CreeperSpiderTest, SpiderClimbsWallItIsBlockedBy) {
    HostileHarness h;
    h.night();
    // 3-block wall (rows y = 1..3, top face at y = 3.5) between spider and player.
    h.blocks.fill(2, 1, -5, 7, 3, 5, STONE);
    Spider s;
    place(s, glm::vec3(-2.0f, 0.5f, 0));
    h.placePlayer(glm::vec3(5.0f, 3.5f, 0));

    float maxY = s.pos.y;
    for (int i = 0; i < 120; ++i) {
        h.run(s, 1);
        maxY = std::max(maxY, s.pos.y);
    }
    EXPECT_GE(maxY, 3.5f - 1e-3f) << "climbed to the top of the wall";
    EXPECT_NEAR(s.pos.y, 3.5f, 0.05f) << "standing on top";
    EXPECT_GT(s.pos.x, 2.0f);
    EXPECT_FLOAT_EQ(s.health, 16.0f) << "no fall damage from climbing";
}

TEST(CreeperSpiderTest, ClimbingIsOptIn) {
    HostileHarness h;
    h.blocks.fill(2, 1, -5, 7, 3, 5, STONE);
    TestMob m; // default climbsWalls() == false
    place(m, glm::vec3(-2.0f, 0.5f, 0));
    m.navigateTo(glm::vec3(5.0f, 3.5f, 0), 0.15f, false);
    float maxY = m.pos.y;
    for (int i = 0; i < 80; ++i) {
        h.run(m, 1);
        maxY = std::max(maxY, m.pos.y);
    }
    EXPECT_LT(maxY, 2.0f) << "a plain mob only hops ~1.25 blocks";
}

TEST(CreeperSpiderTest, SpiderPeacefulInDaylightUntilHurt) {
    HostileHarness h; // sky 15, no darkening → light 15
    Spider s;
    place(s, glm::vec3(0, 0.5f, 0));
    h.placePlayer(glm::vec3(3.0f, 0.5f, 0));

    for (int i = 0; i < 100; ++i) {
        h.run(s, 1);
        EXPECT_FALSE(s.isAttacking());
    }
    EXPECT_EQ(h.playerHits, 0);
    EXPECT_EQ(s.leapCount(), 0);

    h.placePlayer(s.pos + glm::vec3(2.5f, 0, 0));
    ASSERT_TRUE(s.damage(1.0f, h.ctx.playerFeet, 0.0f));
    EXPECT_TRUE(s.isProvoked());
    h.run(s, 1);
    EXPECT_TRUE(s.isAttacking()) << "provoked spiders fight in daylight";
    h.run(s, 60);
    EXPECT_GT(h.playerHits, 0);
}

TEST(CreeperSpiderTest, SpiderHostileInDarkness) {
    HostileHarness h;
    h.night();
    Spider s;
    place(s, glm::vec3(0, 0.5f, 0));
    h.placePlayer(glm::vec3(6.0f, 0.5f, 0));
    EXPECT_TRUE(s.inDarkness(h.ctx));
    h.run(s, 1);
    EXPECT_TRUE(s.isAttacking());
    h.run(s, 80);
    EXPECT_GT(h.playerHits, 0);
}

TEST(CreeperSpiderTest, LeapAppliesUpwardVelocity) {
    Spider s;
    place(s, glm::vec3(0, 0.5f, 0));
    s.leapToward(glm::vec3(3.0f, 0.5f, 0));
    EXPECT_FLOAT_EQ(s.velocity.y, Spider::LEAP_UP);
    EXPECT_NEAR(s.velocity.x, Spider::LEAP_HORIZONTAL, 1e-5f);
    EXPECT_NEAR(s.velocity.z, 0.0f, 1e-5f);

    // Driven by the AI: from 3 blocks away in the dark a spider leaps
    // (10 %/tick) before closing to melee range in most trials.
    int trialsWithLeap = 0;
    for (int trial = 0; trial < 10; ++trial) {
        HostileHarness h;
        h.rng.seed(1000u + static_cast<unsigned>(trial));
        h.night();
        Spider sp;
        place(sp, glm::vec3(0, 0.5f, 0));
        h.placePlayer(glm::vec3(3.5f, 0.5f, 0));
        float maxY = sp.pos.y;
        for (int i = 0; i < 40 && sp.leapCount() == 0; ++i) h.run(sp, 1);
        if (sp.leapCount() == 0) continue;
        ++trialsWithLeap;
        EXPECT_GT(sp.velocity.y, 0.2f) << "still rising the tick after the pounce";
        h.run(sp, 3);
        maxY = std::max(maxY, sp.pos.y);
        EXPECT_GT(maxY, 0.9f);
    }
    EXPECT_GT(trialsWithLeap, 0);
}
