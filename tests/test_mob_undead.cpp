#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "mob_test_support.h"
#include "mobs/skeleton.h"
#include "mobs/zombie.h"
#include "projectile.h"

namespace {

// Player standing on the harness floor (top face y = 0.5) at (x, z).
void placePlayer(MobHarness& h, float x, float z) {
    h.ctx.playerTargetable = true;
    h.ctx.playerFeet = glm::vec3(x, 0.5f, z);
    h.ctx.playerEye = h.ctx.playerFeet + glm::vec3(0.0f, 1.62f, 0.0f);
}

template <typename T> void placeMob(T& m, float x, float z) {
    m.pos = m.prevPos = glm::vec3(x, 0.5f, z);
    m.onGround = true;
}

// The renderer draws flames for exactly the mobs that are burning().
bool flameVisible(const UndeadMob& m) {
    return m.burning();
}

} // namespace

// --- Registry -----------------------------------------------------------------

TEST(UndeadTest, RegistryRows) {
    const MobInfo& z = mobInfo(MobType::Zombie);
    EXPECT_STREQ(z.name, "zombie");
    EXPECT_EQ(z.category, MobCategory::Hostile);
    EXPECT_FLOAT_EQ(z.halfWidth, 0.3f);
    EXPECT_FLOAT_EQ(z.height, 1.95f);
    EXPECT_FLOAT_EQ(z.maxHealth, 20.0f);
    EXPECT_EQ(z.spawnBiomes, ALL_LAND_BIOMES);
    EXPECT_EQ(z.groupMin, 1);
    EXPECT_EQ(z.groupMax, 4);
    EXPECT_EQ(z.spawnWeight, 100);

    const MobInfo& s = mobInfo(MobType::Skeleton);
    EXPECT_STREQ(s.name, "skeleton");
    EXPECT_EQ(s.category, MobCategory::Hostile);
    EXPECT_FLOAT_EQ(s.halfWidth, 0.3f);
    EXPECT_FLOAT_EQ(s.height, 1.99f);
    EXPECT_FLOAT_EQ(s.maxHealth, 20.0f);
    EXPECT_EQ(s.spawnBiomes, ALL_LAND_BIOMES);
    EXPECT_EQ(s.groupMin, 1);
    EXPECT_EQ(s.groupMax, 4);
    EXPECT_EQ(s.spawnWeight, 100);
    EXPECT_EQ(s.spawnBiomes & biomeBit(BIOME_OCEAN), 0u);

    for (MobType t : {MobType::Zombie, MobType::Skeleton}) {
        auto m = createMob(t);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->type, t);
        EXPECT_FLOAT_EQ(m->health, 20.0f);
        EXPECT_LE(m->model().size(), MAX_MOB_PARTS);
        EXPECT_GT(m->eyeHeight(), 1.5f);
        EXPECT_LT(m->eyeHeight(), m->height);
    }
}

TEST(UndeadTest, SkeletonModelAnatomy) {
    Skeleton s;
    const MobModel& m = s.model();
    for (const char* part : {"head", "eye_r", "eye_l", "nose", "mouth", "tooth", "spine", "sternum", "rib",
                             "pelvis", "arm_r", "leg_l", "bow_grip", "bow_string"})
        EXPECT_GE(m.indexOf(part), 0) << part;

    // Flames glow; bones don't.
    for (const MobPart& p : m.parts)
        EXPECT_EQ(p.emissive, std::string(p.name) == "flame") << p.name;

    // Idle: the bow hangs upright (grip tipped 90° back). Aiming: the
    // grip follows the raised arm instead.
    int grip = m.indexOf("bow_grip");
    EXPECT_NEAR(s.pose(0.0f).rotation[grip].x, -glm::half_pi<float>(), 1e-5f);
    s.aiming = true;
    EXPECT_FLOAT_EQ(s.pose(0.0f).rotation[grip].x, 0.0f);
}

// --- Sunlight burning -----------------------------------------------------------

TEST(UndeadTest, ZombieBurnsInDaylightUnderOpenSky) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = true;
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 1);
    EXPECT_TRUE(z.burning());
    EXPECT_FLOAT_EQ(z.health, 19.0f) << "first fire damage lands on ignition";
    EXPECT_TRUE(flameVisible(z));
    h.run(z, Mob::BURN_INTERVAL);
    EXPECT_FLOAT_EQ(z.health, 18.0f) << "1 damage per second";
    h.run(z, Mob::BURN_INTERVAL);
    EXPECT_FLOAT_EQ(z.health, 17.0f);
}

TEST(UndeadTest, ZombieDoesNotBurnAtNight) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = false;
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 100);
    EXPECT_FALSE(z.burning());
    EXPECT_FLOAT_EQ(z.health, 20.0f);
    EXPECT_FALSE(flameVisible(z));
}

TEST(UndeadTest, ZombieDoesNotBurnInShade) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = true;
    h.blocks.sky = 14; // anything below 15 = something overhead
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 100);
    EXPECT_FALSE(z.burning());
    EXPECT_FLOAT_EQ(z.health, 20.0f);
}

TEST(UndeadTest, WaterPreventsAndExtinguishesBurning) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = true;
    h.blocks.fill(-12, 1, -12, 12, 5, 12, WATER);
    Zombie z;
    z.pos = z.prevPos = glm::vec3(0, 2.0f, 0);
    z.setOnFire(50); // already alight when it falls in
    h.run(z, 60);
    EXPECT_TRUE(z.inWater);
    EXPECT_FALSE(z.burning());
    EXPECT_FLOAT_EQ(z.health, 20.0f);
}

TEST(UndeadTest, FireLingersAfterLeavingSunlight) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = true;
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 1);
    h.ctx.isDay = false; // sun gone — still on fire for the rest of the 8 s
    h.run(z, UndeadMob::SUN_FIRE_TICKS - 2);
    EXPECT_TRUE(z.burning());
    h.run(z, 2);
    EXPECT_FALSE(z.burning());
    EXPECT_FLOAT_EQ(z.health, 20.0f - UndeadMob::SUN_FIRE_TICKS / Mob::BURN_INTERVAL);
}

TEST(UndeadTest, SkeletonBurnsToo) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = true;
    Skeleton s;
    placeMob(s, 0, 0);
    h.run(s, 1);
    EXPECT_TRUE(s.burning());
    EXPECT_LT(s.health, 20.0f);
}

// --- Zombie melee -----------------------------------------------------------

TEST(UndeadTest, ZombieMeleeHitsInReachWithCooldown) {
    MobHarness h;
    h.floor();
    h.ctx.isDay = false;
    std::vector<float> hits;
    h.ctx.onDamagePlayer = [&](float amount, glm::vec3 /*from*/) { hits.push_back(amount); };
    placePlayer(h, 0.8f, 0.0f);
    Zombie z;
    placeMob(z, 0, 0);

    h.run(z, 1);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_FLOAT_EQ(hits[0], Zombie::ATTACK_DAMAGE);
    EXPECT_TRUE(z.aggressive);
    h.run(z, Zombie::ATTACK_COOLDOWN - 1);
    EXPECT_EQ(hits.size(), 1u) << "cooldown blocks the next hit";
    h.run(z, 1);
    EXPECT_EQ(hits.size(), 2u);
}

TEST(UndeadTest, ZombieOutOfReachDoesNotHitButChases) {
    MobHarness h;
    h.floor(20);
    h.ctx.isDay = false;
    int hits = 0;
    h.ctx.onDamagePlayer = [&](float, glm::vec3) { ++hits; };
    placePlayer(h, 10.0f, 0.0f);
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 10);
    EXPECT_EQ(hits, 0);
    EXPECT_TRUE(z.aggressive);
    EXPECT_GT(z.pos.x, 0.5f) << "walks toward the player";
}

TEST(UndeadTest, ZombieIgnoresPlayerBehindWall) {
    MobHarness h;
    h.floor(20);
    h.ctx.isDay = false;
    h.blocks.fill(3, 1, -20, 3, 8, 20, STONE);
    placePlayer(h, 6.0f, 0.0f);
    Zombie z;
    placeMob(z, 0, 0);
    h.run(z, 5);
    EXPECT_FALSE(z.aggressive);
}

// --- Line of sight -----------------------------------------------------------

TEST(UndeadTest, LineOfSightBlockedAndClear) {
    MobHarness h;
    glm::vec3 a(0.0f, 2.0f, 0.0f), b(10.0f, 2.0f, 0.0f);
    EXPECT_TRUE(mobai::hasLineOfSight(h.ctx, a, b));
    h.blocks.set(5, 3, 0, STONE); // beside the ray, not on it
    h.blocks.set(5, 2, 1, STONE);
    EXPECT_TRUE(mobai::hasLineOfSight(h.ctx, a, b));
    h.blocks.set(5, 2, 0, STONE);
    EXPECT_FALSE(mobai::hasLineOfSight(h.ctx, a, b));
    EXPECT_FALSE(mobai::hasLineOfSight(h.ctx, b, a)) << "symmetric";

    // Diagonal through 3D: blocked only by a cell actually crossed.
    glm::vec3 c(0.2f, 1.1f, 0.3f), d(6.7f, 4.2f, 5.9f);
    EXPECT_TRUE(mobai::hasLineOfSight(h.ctx, c, d));
    glm::vec3 mid = (c + d) * 0.5f;
    glm::ivec3 mb = MobContext::blockAtPos(mid);
    h.blocks.set(mb.x, mb.y, mb.z, STONE);
    EXPECT_FALSE(mobai::hasLineOfSight(h.ctx, c, d));

    // Non-solid blocks (water) don't block sight.
    MobHarness w;
    w.blocks.fill(3, 0, -1, 6, 4, 1, WATER);
    EXPECT_TRUE(mobai::hasLineOfSight(w.ctx, a, b));
}

// --- Skeleton bow AI ---------------------------------------------------------

namespace {
struct ShotLog {
    std::vector<ProjectileSpec> shots;
    void attach(MobHarness& h) {
        h.ctx.onSpawnProjectile = [this](const ProjectileSpec& s) { shots.push_back(s); };
    }
};
} // namespace

TEST(UndeadTest, SkeletonFiresAfterDrawTimeWhenInRangeWithSight) {
    MobHarness h;
    h.floor(20);
    h.ctx.isDay = false;
    ShotLog log;
    log.attach(h);
    placePlayer(h, 10.0f, 0.0f);
    Skeleton s;
    s.id = 7;
    placeMob(s, 0, 0);

    h.run(s, RangedBowAttackGoal::DRAW_TICKS - 5);
    EXPECT_TRUE(s.aiming);
    EXPECT_GT(s.bowDrawTicks, 0) << "drawing the bow";
    EXPECT_TRUE(log.shots.empty()) << "still drawing";
    h.run(s, 10);
    ASSERT_EQ(log.shots.size(), 1u);
    const ProjectileSpec& a = log.shots[0];
    EXPECT_EQ(a.shooterId, 7u);
    EXPECT_TRUE(a.hurtsPlayer);
    EXPECT_GE(a.damage, 2.0f);
    EXPECT_LE(a.damage, 4.0f);
    EXPECT_NEAR(glm::length(a.velocity), RangedBowAttackGoal::ARROW_SPEED, 0.3f);
    EXPECT_GT(a.velocity.x, 1.0f) << "aimed at the player on +X";
    EXPECT_GT(a.velocity.y, 0.0f) << "lifted to cancel drop";

    // Cooldown (20-40 ticks) + another 20-tick draw before the next arrow.
    h.run(s, 35);
    EXPECT_EQ(log.shots.size(), 1u);
    h.run(s, 40);
    EXPECT_GE(log.shots.size(), 2u);
}

TEST(UndeadTest, SkeletonHoldsFireWithoutLineOfSight) {
    MobHarness h;
    h.floor(20);
    h.ctx.isDay = false;
    h.blocks.fill(5, 1, -20, 5, 10, 20, STONE);
    ShotLog log;
    log.attach(h);
    placePlayer(h, 10.0f, 0.0f);
    Skeleton s;
    placeMob(s, 0, 0);
    h.run(s, 100);
    EXPECT_TRUE(log.shots.empty());
    EXPECT_FALSE(s.aiming);
}

TEST(UndeadTest, SkeletonHoldsFireOutOfRange) {
    MobHarness h;
    h.floor(12);
    h.ctx.isDay = false;
    ShotLog log;
    log.attach(h);
    placePlayer(h, 40.0f, 0.0f);
    Skeleton s;
    placeMob(s, 0, 0);
    h.run(s, 100);
    EXPECT_TRUE(log.shots.empty());
}

TEST(UndeadTest, SkeletonBacksOffWhenPlayerIsClose) {
    MobHarness h;
    h.floor(20);
    h.ctx.isDay = false;
    placePlayer(h, 3.0f, 0.0f);
    Skeleton s;
    placeMob(s, 0, 0);
    h.run(s, 60);
    EXPECT_LT(s.pos.x, -0.5f) << "strafes away from a player inside 5 blocks";
}

TEST(UndeadTest, AimSolutionHitsPlayerOnFlatGround) {
    for (float dist : {4.0f, 8.0f, 12.0f, 15.0f}) {
        MobHarness h;
        h.floor(24);
        placePlayer(h, dist, 0.0f);
        int hits = 0;
        h.ctx.onDamagePlayer = [&](float, glm::vec3) { ++hits; };

        Skeleton s;
        placeMob(s, 0, 0);
        ProjectileSpec spec;
        spec.pos = RangedBowAttackGoal::launchPos(s);
        spec.velocity = RangedBowAttackGoal::aimVelocity(spec.pos, h.ctx.playerFeet);
        Projectile p(spec);
        for (int i = 0; i < 100 && !p.removed && !p.stuck; ++i) p.tick(h.ctx, nullptr);
        EXPECT_EQ(hits, 1) << "arrow missed at distance " << dist << ", ended at y=" << p.pos.y;
    }
}
