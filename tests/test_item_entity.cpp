#include <gtest/gtest.h>
#include "item_entity.h"
#include "mob_drops.h"

// Floor of solid blocks at y = 0 (top face at y = 0.5).
static bool floorAt0(int /*x*/, int y, int /*z*/) {
    return y <= 0;
}

static ItemEntity dropAt(glm::vec3 pos, glm::vec3 vel = glm::vec3(0.0f)) {
    ItemEntity e;
    e.id = blockItem(DIRT);
    e.count = 1;
    e.pos = e.prevPos = pos;
    e.velocity = vel;
    return e;
}

TEST(ItemEntityTest, FallsAndLandsOnTheFloor) {
    ItemEntity e = dropAt(glm::vec3(0.0f, 5.0f, 0.0f));
    for (int i = 0; i < 100; i++) e.tick(floorAt0);
    EXPECT_TRUE(e.onGround);
    EXPECT_FLOAT_EQ(e.pos.y, 0.5f);
    EXPECT_FLOAT_EQ(e.velocity.y, 0.0f);
}

TEST(ItemEntityTest, SlidesToAStopOnTheGround) {
    ItemEntity e = dropAt(glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.3f, 0.0f, 0.0f));
    for (int i = 0; i < 40; i++) e.tick(floorAt0);
    EXPECT_GT(e.pos.x, 0.3f);
    EXPECT_LT(std::abs(e.velocity.x), 1e-3f);
}

TEST(ItemEntityTest, WallStopsIt) {
    auto wall = [](int x, int y, int /*z*/) { return y <= 0 || x >= 2; };
    ItemEntity e = dropAt(glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f));
    for (int i = 0; i < 20; i++) e.tick(wall);
    EXPECT_LE(e.pos.x + ItemEntity::HALF_WIDTH, 1.5f + 1e-4f);
}

TEST(ItemEntityTest, PickupDelayAndDespawn) {
    ItemEntity e = dropAt(glm::vec3(0.0f, 0.5f, 0.0f));
    EXPECT_FALSE(e.canBePickedUp());
    for (int i = 0; i < ItemEntity::PICKUP_DELAY; i++) e.tick(floorAt0);
    EXPECT_TRUE(e.canBePickedUp());
    for (int i = ItemEntity::PICKUP_DELAY; i < ItemEntity::DESPAWN_AGE; i++) e.tick(floorAt0);
    EXPECT_TRUE(e.removed);
}

TEST(ItemEntityTest, RenderPosInterpolates) {
    ItemEntity e = dropAt(glm::vec3(0.0f, 0.5f, 0.0f));
    e.prevPos = glm::vec3(0.0f);
    e.pos = glm::vec3(2.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(e.renderPos(0.25f).x, 0.5f);
}

TEST(ItemEntityTest, NearbyStacksOfTheSameItemMerge) {
    ItemEntity a = dropAt(glm::vec3(0.0f, 0.5f, 0.0f));
    ItemEntity b = dropAt(glm::vec3(0.3f, 0.5f, 0.0f));
    a.count = 60;
    b.count = 10;
    EXPECT_TRUE(mergeItems(a, b, 64));
    EXPECT_EQ(a.count, 64);
    EXPECT_EQ(b.count, 6);
    EXPECT_FALSE(b.removed);

    ItemEntity c = dropAt(glm::vec3(0.0f, 0.5f, 0.2f));
    c.count = 3;
    a.count = 10;
    EXPECT_TRUE(mergeItems(a, c, 64));
    EXPECT_TRUE(c.removed);
}

TEST(ItemEntityTest, DifferentOrDistantItemsDontMerge) {
    ItemEntity a = dropAt(glm::vec3(0.0f, 0.5f, 0.0f));
    ItemEntity far = dropAt(glm::vec3(2.0f, 0.5f, 0.0f));
    EXPECT_FALSE(mergeItems(a, far, 64));
    ItemEntity other = dropAt(glm::vec3(0.1f, 0.5f, 0.0f));
    other.id = blockItem(SAND);
    EXPECT_FALSE(mergeItems(a, other, 64));
}

TEST(MobDropTest, AnimalsDropTheirMeat) {
    EXPECT_EQ(mobDrop(MobType::Pig, 0.0f).id, ITEM_RAW_PORKCHOP);
    EXPECT_EQ(mobDrop(MobType::Pig, 0.0f).count, 1);
    EXPECT_EQ(mobDrop(MobType::Pig, 0.999f).count, 3);
    EXPECT_EQ(mobDrop(MobType::Cow, 0.5f).id, ITEM_RAW_BEEF);
    EXPECT_EQ(mobDrop(MobType::Sheep, 0.999f).count, 2);
    EXPECT_EQ(mobDrop(MobType::Chicken, 0.7f).id, ITEM_RAW_CHICKEN);
    EXPECT_EQ(mobDrop(MobType::Chicken, 0.7f).count, 1);
}

TEST(MobDropTest, HostilesDropNothingYet) {
    EXPECT_EQ(mobDrop(MobType::Zombie, 0.5f).count, 0);
    EXPECT_EQ(mobDrop(MobType::Creeper, 0.5f).count, 0);
    EXPECT_EQ(mobDrop(MobType::CreeperCow, 0.5f).count, 0);
}
