#include <gtest/gtest.h>

#include <set>
#include <string>

#include "collision.h"
#include "mob_test_support.h"
#include "mobs/pig.h"
#include "projectile.h"

// --- Damage / knockback / death ---------------------------------------------

TEST(MobCombatTest, DamageReducesHealthAndFlashes) {
    TestMob m;
    ASSERT_FLOAT_EQ(m.health, 10.0f);
    EXPECT_TRUE(m.damage(3.0f, glm::vec3(-1, 0, 0)));
    EXPECT_FLOAT_EQ(m.health, 7.0f);
    EXPECT_EQ(m.hurtTicks, Mob::HURT_FLASH_TICKS);
    EXPECT_TRUE(m.hurtSinceLastAi);
    EXPECT_FALSE(m.dying);
}

TEST(MobCombatTest, InvulnerabilityFramesBlockRapidHits) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0, 0.5f, 0);
    EXPECT_TRUE(m.damage(1.0f, glm::vec3(-1, 0.5f, 0)));
    EXPECT_FALSE(m.damage(1.0f, glm::vec3(-1, 0.5f, 0))) << "second hit inside i-frames";
    h.run(m, Mob::INVULNERABLE_TICKS - 1);
    EXPECT_FALSE(m.damage(1.0f, glm::vec3(-1, 0.5f, 0)));
    h.run(m, 1);
    EXPECT_TRUE(m.damage(1.0f, glm::vec3(-1, 0.5f, 0)));
    EXPECT_FLOAT_EQ(m.health, 8.0f);
}

TEST(MobCombatTest, KnockbackPushesAwayAndUp) {
    TestMob m;
    m.pos = glm::vec3(0, 0.5f, 0);
    m.onGround = true;
    m.damage(1.0f, glm::vec3(0, 0.5f, -2.0f)); // source behind on -Z
    EXPECT_NEAR(m.velocity.z, Mob::KNOCKBACK, 1e-5f);
    EXPECT_NEAR(m.velocity.x, 0.0f, 1e-5f);
    EXPECT_NEAR(m.velocity.y, 0.4f, 1e-5f);
}

TEST(MobCombatTest, NoKnockbackHopWhenAirborne) {
    TestMob m;
    m.onGround = false;
    m.velocity = glm::vec3(0, -0.3f, 0);
    m.damage(1.0f, glm::vec3(-1, 0, 0));
    EXPECT_FLOAT_EQ(m.velocity.y, -0.3f);
    EXPECT_GT(m.velocity.x, 0.0f);
}

TEST(MobCombatTest, DeathTipsOverForTwentyTicksThenRemoves) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0, 0.5f, 0);
    EXPECT_TRUE(m.damage(50.0f, glm::vec3(-1, 0.5f, 0)));
    EXPECT_TRUE(m.dying);
    EXPECT_FLOAT_EQ(m.health, 0.0f);
    EXPECT_FALSE(m.isAlive());
    h.run(m, Mob::DEATH_TICKS - 1);
    EXPECT_FALSE(m.removed);
    EXPECT_GT(m.deathProgress(0.0f), 0.9f);
    h.run(m, 1);
    EXPECT_TRUE(m.removed);
    EXPECT_FLOAT_EQ(m.deathProgress(1.0f), 1.0f);
}

TEST(MobCombatTest, DyingMobIgnoresFurtherDamage) {
    TestMob m;
    m.kill();
    EXPECT_TRUE(m.dying);
    EXPECT_FALSE(m.damage(5.0f, glm::vec3(0)));
}

TEST(MobCombatTest, HurtAndDeathQueueSounds) {
    TestMob a;
    a.damage(1.0f, glm::vec3(0));
    EXPECT_TRUE(a.pendingSounds & (1u << static_cast<unsigned>(MobSoundEvent::Hurt)));
    TestMob b;
    b.kill();
    EXPECT_TRUE(b.pendingSounds & (1u << static_cast<unsigned>(MobSoundEvent::Death)));
}

// --- Ray vs AABB (player melee targeting) -----------------------------------

TEST(MobCombatTest, RayHitsMobHitbox) {
    TestMob m;
    m.pos = glm::vec3(5.0f, 0.5f, 0.0f); // hitbox x in [4.55, 5.45], y in [0.5, 1.4]
    float t = 0.0f;
    EXPECT_TRUE(m.rayHit(glm::vec3(0, 1.0f, 0), glm::vec3(1, 0, 0), 10.0f, t));
    EXPECT_NEAR(t, 4.55f, 1e-4f);
    EXPECT_FALSE(m.rayHit(glm::vec3(0, 1.0f, 0), glm::vec3(-1, 0, 0), 10.0f, t)) << "facing away";
    EXPECT_FALSE(m.rayHit(glm::vec3(0, 1.0f, 0), glm::vec3(1, 0, 0), 3.0f, t)) << "out of reach";
    EXPECT_FALSE(m.rayHit(glm::vec3(0, 2.0f, 0), glm::vec3(1, 0, 0), 10.0f, t)) << "passes over the top";
}

TEST(MobCombatTest, RayAabbEdgeCases) {
    float t = -1.0f;
    // Origin inside → t = 0.
    EXPECT_TRUE(rayIntersectsAABB(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(-1), glm::vec3(1), 5.0f, t));
    EXPECT_FLOAT_EQ(t, 0.0f);
    // Axis-parallel ray outside the slab never hits.
    EXPECT_FALSE(rayIntersectsAABB(glm::vec3(2, 0, -5), glm::vec3(0, 0, 1), glm::vec3(-1), glm::vec3(1), 50.0f, t));
    // Diagonal hit.
    EXPECT_TRUE(rayIntersectsAABB(glm::vec3(-3, -3, 0), glm::normalize(glm::vec3(1, 1, 0)), glm::vec3(-1),
                                  glm::vec3(1), 10.0f, t));
    EXPECT_NEAR(t, std::sqrt(8.0f), 1e-4f);
}

// --- Registry ----------------------------------------------------------------

TEST(MobRegistryTest, EveryMobTypeHasAValidRow) {
    const MobInfo* table = mobRegistry();
    std::set<std::string> names;
    for (int i = 0; i < MOB_TYPE_COUNT; ++i) {
        const MobInfo& info = table[i];
        SCOPED_TRACE(i);
        EXPECT_EQ(static_cast<int>(info.type), i) << "row out of enum order";
        ASSERT_NE(info.name, nullptr);
        EXPECT_FALSE(std::string(info.name).empty());
        EXPECT_TRUE(names.insert(info.name).second) << "duplicate name " << info.name;
        ASSERT_NE(info.create, nullptr);
        EXPECT_GT(info.halfWidth, 0.0f);
        EXPECT_GT(info.height, 0.0f);
        EXPECT_GT(info.maxHealth, 0.0f);
        EXPECT_LE(info.groupMin, info.groupMax);
        EXPECT_GE(info.groupMin, 1);

        std::unique_ptr<Mob> m = createMob(info.type);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->type, info.type);
        EXPECT_FLOAT_EQ(m->health, info.maxHealth);
        EXPECT_FLOAT_EQ(m->halfWidth, info.halfWidth);

        // Model sanity: fits the pose arrays, parents precede children.
        const MobModel& model = m->model();
        EXPECT_GT(model.size(), 0);
        EXPECT_LE(model.size(), MAX_MOB_PARTS);
        for (int p = 0; p < model.size(); ++p) EXPECT_LT(model.parts[p].parent, p);
        // pose() must be callable on a fresh mob.
        (void)m->pose(0.5f);
    }
}

// --- Pig / stock goals ---------------------------------------------------------

TEST(MobAiTest, PigPanicsWhenHurt) {
    MobHarness h;
    h.floor(20);
    Pig pig;
    pig.pos = pig.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(pig, 3);
    pig.damage(1.0f, glm::vec3(-1.0f, 0.5f, 0.0f));
    pig.tick(h.ctx);
    EXPECT_TRUE(pig.goals.isRunning<PanicGoal>());
    EXPECT_TRUE(pig.isNavigating());
    glm::vec3 start = pig.pos;
    h.run(pig, 40);
    EXPECT_GT(glm::distance(start, pig.pos), 2.0f) << "panicking pig should run";
    // Panic wears off after ~5 s.
    h.run(pig, 120);
    EXPECT_FALSE(pig.goals.isRunning<PanicGoal>());
}

TEST(MobAiTest, PigWandersEventually) {
    MobHarness h;
    h.floor(20);
    Pig pig;
    pig.pos = pig.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(pig, 1200);
    EXPECT_GT(glm::length(glm::vec2(pig.pos.x, pig.pos.z)), 0.5f);
    EXPECT_FLOAT_EQ(pig.pos.y, 0.5f) << "flat floor — stays grounded";
}

namespace {
class TestBrute : public TestMob {
  public:
    TestBrute() { goals.add(2, std::make_unique<ChaseAndMeleeGoal>(0.12f, 3.0f, 16.0f, 20)); }
};
} // namespace

TEST(MobAiTest, ChaseAndMeleeHitsPlayerWithCooldown) {
    MobHarness h;
    h.floor(20);
    int hits = 0;
    float lastAmount = 0.0f;
    h.ctx.onDamagePlayer = [&](float amount, glm::vec3) {
        ++hits;
        lastAmount = amount;
    };
    h.ctx.playerTargetable = true;
    h.ctx.playerFeet = glm::vec3(6.0f, 0.5f, 0.0f);
    h.ctx.playerEye = h.ctx.playerFeet + glm::vec3(0, 1.62f, 0);

    TestBrute b;
    b.pos = b.prevPos = glm::vec3(0, 0.5f, 0);
    h.run(b, 80);
    EXPECT_GT(b.pos.x, 4.0f) << "should close the distance";
    EXPECT_GE(hits, 1);
    EXPECT_FLOAT_EQ(lastAmount, 3.0f);
    // 80 ticks with a 20-tick cooldown can't produce more than 4 hits.
    EXPECT_LE(hits, 4);

    // Untargetable (dead) player → no more hits.
    int before = hits;
    h.ctx.playerTargetable = false;
    h.run(b, 60);
    EXPECT_EQ(hits, before);
}

// --- Projectiles -------------------------------------------------------------

TEST(ProjectileTest, HitsPlayerAndDisappears) {
    MobHarness h;
    int hits = 0;
    h.ctx.onDamagePlayer = [&](float, glm::vec3) { ++hits; };
    h.ctx.playerTargetable = true;
    h.ctx.playerFeet = glm::vec3(10.0f, 0.5f, 0.0f);
    ProjectileSpec spec;
    spec.pos = glm::vec3(0.0f, 1.5f, 0.0f);
    spec.velocity = glm::vec3(1.5f, 0.0f, 0.0f);
    spec.gravity = 0.0f;
    Projectile p(spec);
    for (int i = 0; i < 20 && !p.removed; ++i) p.tick(h.ctx, nullptr);
    EXPECT_TRUE(p.removed);
    EXPECT_EQ(hits, 1);
}

TEST(ProjectileTest, SticksInWallAndExpires) {
    MobHarness h;
    h.blocks.fill(5, 0, -2, 5, 4, 2, STONE); // wall face at x = 4.5
    ProjectileSpec spec;
    spec.pos = glm::vec3(0.0f, 2.0f, 0.0f);
    spec.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
    spec.gravity = 0.0f;
    Projectile p(spec);
    for (int i = 0; i < 10; ++i) p.tick(h.ctx, nullptr);
    EXPECT_TRUE(p.stuck);
    EXPECT_FALSE(p.removed);
    EXPECT_LT(p.pos.x, 4.5f);
    for (int i = 0; i < Projectile::STUCK_TICKS; ++i) p.tick(h.ctx, nullptr);
    EXPECT_TRUE(p.removed);
}

TEST(ProjectileTest, ArcsUnderGravity) {
    MobHarness h;
    ProjectileSpec spec;
    spec.pos = glm::vec3(0.0f, 50.0f, 0.0f);
    spec.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
    Projectile p(spec);
    for (int i = 0; i < 10; ++i) p.tick(h.ctx, nullptr);
    EXPECT_LT(p.pos.y, 50.0f);
    EXPECT_LT(p.velocity.y, 0.0f);
}
