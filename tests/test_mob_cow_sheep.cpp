#include <gtest/gtest.h>

#include <array>
#include <random>

#include "mob_test_support.h"
#include "mobs/cow.h"
#include "mobs/sheep.h"

// --- Registry ------------------------------------------------------------------

TEST(CowSheepTest, RegistryRows) {
    const MobInfo& cow = mobInfo(MobType::Cow);
    EXPECT_STREQ(cow.name, "cow");
    EXPECT_EQ(cow.category, MobCategory::Passive);
    EXPECT_FLOAT_EQ(cow.maxHealth, 10.0f);
    EXPECT_FLOAT_EQ(cow.halfWidth, 0.45f);
    EXPECT_FLOAT_EQ(cow.height, 1.4f);
    EXPECT_TRUE(cow.spawnBiomes & biomeBit(BIOME_PLAINS));
    EXPECT_TRUE(cow.spawnBiomes & biomeBit(BIOME_FOREST));
    EXPECT_FALSE(cow.spawnBiomes & biomeBit(BIOME_DESERT));
    EXPECT_EQ(cow.groupMin, 2);
    EXPECT_EQ(cow.groupMax, 4);

    const MobInfo& sheep = mobInfo(MobType::Sheep);
    EXPECT_STREQ(sheep.name, "sheep");
    EXPECT_EQ(sheep.category, MobCategory::Passive);
    EXPECT_FLOAT_EQ(sheep.maxHealth, 8.0f);
    EXPECT_FLOAT_EQ(sheep.halfWidth, 0.45f);
    EXPECT_FLOAT_EQ(sheep.height, 1.3f);
    EXPECT_TRUE(sheep.spawnBiomes & biomeBit(BIOME_PLAINS));
    EXPECT_TRUE(sheep.spawnBiomes & biomeBit(BIOME_FOREST));
    EXPECT_TRUE(sheep.spawnBiomes & biomeBit(BIOME_TUNDRA));
    EXPECT_FALSE(sheep.spawnBiomes & biomeBit(BIOME_OCEAN));
    EXPECT_EQ(sheep.groupMin, 2);
    EXPECT_EQ(sheep.groupMax, 4);

    EXPECT_EQ(createMob(MobType::Cow)->type, MobType::Cow);
    EXPECT_EQ(createMob(MobType::Sheep)->type, MobType::Sheep);
}

// --- Models --------------------------------------------------------------------

TEST(CowSheepTest, CowModelParts) {
    Cow cow;
    const MobModel& m = cow.model();
    for (const char* name : {"body", "head", "udder", "horn_l", "horn_r", "muzzle", "leg_fl", "leg_fr", "leg_bl",
                             "leg_br", "spot_back_l", "head_spot"})
        EXPECT_GE(m.indexOf(name), 0) << name;
    EXPECT_LE(m.size(), MAX_MOB_PARTS);
    // Legs reach the ground: hip at 12 px, 12 px long.
    const MobPart& leg = m.parts[m.indexOf("leg_fl")];
    EXPECT_NEAR(leg.pivot.y + leg.center.y - leg.halfSize.y, 0.0f, 1e-5f);
    // Udder hangs under the body.
    const MobPart& body = m.parts[m.indexOf("body")];
    const MobPart& udder = m.parts[m.indexOf("udder")];
    EXPECT_LT(udder.pivot.y, -body.halfSize.y + 1e-5f);
}

TEST(CowSheepTest, SheepModelPartsAndWoolOverlay) {
    Sheep sheep;
    const MobModel& m = sheep.model();
    for (const char* name : {"body", "body_wool", "head", "head_wool", "leg_fl", "leg_br", "nose"})
        EXPECT_GE(m.indexOf(name), 0) << name;
    // Fleece is bigger than the body it wraps.
    const MobPart& body = m.parts[m.indexOf("body")];
    const MobPart& wool = m.parts[m.indexOf("body_wool")];
    EXPECT_GT(wool.halfSize.x, body.halfSize.x);
    EXPECT_GT(wool.halfSize.y, body.halfSize.y);
    EXPECT_GT(wool.halfSize.z, body.halfSize.z);

    int woolIdx = m.indexOf("body_wool");
    int headWool = m.indexOf("head_wool");
    EXPECT_FALSE(sheep.pose(0.0f).hidden[woolIdx]);
    ASSERT_TRUE(sheep.shear());
    MobPose shorn = sheep.pose(0.0f);
    EXPECT_TRUE(shorn.hidden[woolIdx]);
    EXPECT_TRUE(shorn.hidden[headWool]);
    EXPECT_FALSE(shorn.hidden[m.indexOf("head")]);
    int hiddenCount = 0;
    for (int i = 0; i < m.size(); ++i) hiddenCount += shorn.hidden[i] ? 1 : 0;
    EXPECT_EQ(hiddenCount, 6) << "body + head + 4 leg fleeces";
}

TEST(CowSheepTest, SheepModelTintFollowsWoolColor) {
    Sheep sheep;
    for (int i = 0; i < WOOL_COLOR_COUNT; ++i) {
        auto c = static_cast<WoolColor>(i);
        sheep.setWoolColor(c);
        const MobModel& m = sheep.model();
        glm::vec3 rgb = m.parts[m.indexOf("body_wool")].color;
        EXPECT_EQ(rgb, Sheep::woolRgb(c)) << i;
        // Skin parts don't change with the fleece.
        EXPECT_EQ(m.parts[m.indexOf("head")].color, Sheep().model().parts[m.indexOf("head")].color);
    }
}

// --- Wool color odds ------------------------------------------------------------

TEST(CowSheepTest, WoolColorDistributionMatchesMinecraft) {
    std::mt19937 rng(2024u);
    constexpr int N = 400000;
    std::array<int, WOOL_COLOR_COUNT> counts{};
    for (int i = 0; i < N; ++i) ++counts[static_cast<int>(Sheep::randomWoolColor(rng))];
    auto frac = [&](WoolColor c) { return static_cast<double>(counts[static_cast<int>(c)]) / N; };
    EXPECT_NEAR(frac(WoolColor::White), 0.81836, 0.005);
    EXPECT_NEAR(frac(WoolColor::LightGray), 0.05, 0.003);
    EXPECT_NEAR(frac(WoolColor::Gray), 0.05, 0.003);
    EXPECT_NEAR(frac(WoolColor::Black), 0.05, 0.003);
    EXPECT_NEAR(frac(WoolColor::Brown), 0.03, 0.002);
    EXPECT_NEAR(frac(WoolColor::Pink), 0.00164, 0.0004);
    EXPECT_GT(counts[static_cast<int>(WoolColor::Pink)], 0);
}

// --- Shearing ----------------------------------------------------------------------

TEST(CowSheepTest, ShearIsIdempotent) {
    Sheep sheep;
    EXPECT_FALSE(sheep.isSheared());
    EXPECT_TRUE(sheep.shear());
    EXPECT_TRUE(sheep.isSheared());
    EXPECT_FALSE(sheep.shear()) << "second shear is a no-op";
    EXPECT_TRUE(sheep.isSheared());
    sheep.regrowWool();
    EXPECT_FALSE(sheep.isSheared());

    Sheep dead;
    dead.kill();
    EXPECT_FALSE(dead.shear()) << "can't shear a dying sheep";
    EXPECT_FALSE(dead.isSheared());
}

// --- Grazing -----------------------------------------------------------------------

namespace {
// Grass floor whose top face is at y = 0.5 (block row y = 0).
void grassFloor(MobHarness& h, int radius = 12) {
    h.blocks.fill(-radius, 0, -radius, radius, 0, radius, GRASS);
}
} // namespace

TEST(CowSheepTest, EatGrassGoalRegrowsWool) {
    MobHarness h;
    grassFloor(h);
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(sheep, 3); // settle onto the ground
    ASSERT_TRUE(sheep.onGround);
    sheep.shear();

    EatGrassGoal goal(sheep, 1.0f); // always fires
    ASSERT_TRUE(goal.canStart(sheep, h.ctx));
    goal.start(sheep, h.ctx);
    EXPECT_EQ(sheep.eatAnimTicks, EatGrassGoal::DURATION_TICKS);
    EXPECT_GT(sheep.headEatDrop(1.0f), 0.0f);

    int ticks = 0;
    while (goal.shouldContinue(sheep, h.ctx) && ticks < 100) {
        goal.tick(sheep, h.ctx);
        ++ticks;
        if (sheep.eatAnimTicks > EatGrassGoal::EAT_AT_TICK) {
            EXPECT_TRUE(sheep.isSheared()) << "eats near the end";
        }
        if (sheep.eatAnimTicks == 20) {
            EXPECT_FLOAT_EQ(sheep.headEatDrop(0.5f), 1.0f) << "head down mid-graze";
        }
    }
    goal.stop(sheep, h.ctx);
    EXPECT_EQ(ticks, EatGrassGoal::DURATION_TICKS);
    EXPECT_FALSE(sheep.isSheared()) << "grazing regrows the fleece";
    EXPECT_EQ(sheep.eatAnimTicks, 0);
    EXPECT_FLOAT_EQ(sheep.headEatDrop(0.0f), 0.0f);
}

TEST(CowSheepTest, EatGrassAlsoEatsTallGrass) {
    MobHarness h;
    h.floor(); // stone: only the tall grass is edible
    h.blocks.set(0, 1, 0, TALL_GRASS);
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(sheep, 3);
    EXPECT_TRUE(EatGrassGoal::hasGrass(sheep, h.ctx));
    h.blocks.set(0, 1, 0, AIR);
    EXPECT_FALSE(EatGrassGoal::hasGrass(sheep, h.ctx));
}

TEST(CowSheepTest, NoGrazingWithoutGrass) {
    MobHarness h;
    h.floor(); // stone
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(sheep, 3);
    EatGrassGoal goal(sheep, 1.0f);
    EXPECT_FALSE(goal.canStart(sheep, h.ctx));
}

TEST(CowSheepTest, ShearedSheepEventuallyRegrowsOnItsOwn) {
    MobHarness h;
    grassFloor(h, 40);
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    sheep.shear();
    bool sawGrazing = false;
    for (int i = 0; i < 20000 && sheep.isSheared(); ++i) {
        sheep.tick(h.ctx);
        sawGrazing = sawGrazing || sheep.goals.isRunning<EatGrassGoal>();
    }
    EXPECT_TRUE(sawGrazing);
    EXPECT_FALSE(sheep.isSheared()) << "~1/1000 per tick should fire well within 20000 ticks";
}

TEST(CowSheepTest, GrazingSheepStandsStillWithHeadDown) {
    MobHarness h;
    grassFloor(h);
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(sheep, 3);
    EatGrassGoal goal(sheep, 1.0f);
    goal.start(sheep, h.ctx);
    for (int i = 0; i < 10; ++i) goal.tick(sheep, h.ctx);
    EXPECT_FALSE(sheep.isNavigating());
    const MobModel& m = sheep.model();
    int head = m.indexOf("head");
    MobPose p = sheep.pose(0.5f);
    EXPECT_LT(p.offset[head].y, 0.0f) << "neck lowered";
    // pi/5 +- 0.22 nibble bob: always well below level.
    EXPECT_GT(p.rotation[head].x, 0.35f) << "face tipped toward the grass";
}

// --- Panic -------------------------------------------------------------------------

template <typename T> void expectPanicsWhenHurt() {
    MobHarness h;
    h.floor(20);
    T mob;
    mob.pos = mob.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(mob, 3);
    mob.damage(1.0f, glm::vec3(-1.0f, 0.5f, 0.0f));
    mob.tick(h.ctx);
    EXPECT_TRUE(mob.goals.template isRunning<PanicGoal>());
    EXPECT_TRUE(mob.isNavigating());
    glm::vec3 start = mob.pos;
    h.run(mob, 40);
    EXPECT_GT(glm::distance(start, mob.pos), 2.0f);
    h.run(mob, 120);
    EXPECT_FALSE(mob.goals.template isRunning<PanicGoal>());
}

TEST(CowSheepTest, CowPanicsWhenHurt) {
    expectPanicsWhenHurt<Cow>();
}

TEST(CowSheepTest, SheepPanicsWhenHurt) {
    expectPanicsWhenHurt<Sheep>();
}

TEST(CowSheepTest, PanicInterruptsGrazing) {
    MobHarness h;
    grassFloor(h, 20);
    Sheep sheep;
    sheep.pos = sheep.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(sheep, 3);
    // Run until it starts grazing on its own.
    for (int i = 0; i < 20000 && !sheep.goals.isRunning<EatGrassGoal>(); ++i) sheep.tick(h.ctx);
    ASSERT_TRUE(sheep.goals.isRunning<EatGrassGoal>());
    sheep.damage(1.0f, sheep.pos + glm::vec3(-1.0f, 0.0f, 0.0f));
    sheep.tick(h.ctx);
    EXPECT_TRUE(sheep.goals.isRunning<PanicGoal>());
    EXPECT_FALSE(sheep.goals.isRunning<EatGrassGoal>());
    EXPECT_EQ(sheep.eatAnimTicks, 0);
}
