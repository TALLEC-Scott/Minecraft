#pragma once

#include <cstdint>
#include <random>

#include "mob.h"

// Natural fleece colors (Minecraft's spawn odds, see randomWoolColor()).
enum class WoolColor : uint8_t { White, LightGray, Gray, Black, Brown, Pink, Count };
inline constexpr int WOOL_COLOR_COUNT = static_cast<int>(WoolColor::Count);

class Sheep;

// Sheep grazing: rarely (~1/1000 per tick) stops, lowers its head for 40
// ticks and, 4 ticks before the end, eats the tall grass it stands in or
// turns the GRASS block underfoot into DIRT. Eating regrows sheared wool.
// Holds MOVE + LOOK so the sheep stands still with its head down.
class EatGrassGoal : public MobGoal {
  public:
    static constexpr int DURATION_TICKS = 40;
    static constexpr int EAT_AT_TICK = 4; // remaining ticks when the block is consumed

    explicit EatGrassGoal(Sheep& sheep, float chancePerTick = 1.0f / 1000.0f)
        : MobGoal(GOAL_MOVE | GOAL_LOOK), sheep(sheep), chancePerTick(chancePerTick) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void stop(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

    // True if there is something edible at / under `mob`'s feet.
    static bool hasGrass(const Mob& mob, const MobContext& ctx);

  private:
    Sheep& sheep;
    float chancePerTick;
};

// Passive quadruped, 8 HP. Wanders, grazes (EatGrassGoal), looks at the
// player, panics when hurt. Right-click shears it (no items yet, so the
// wool just disappears); grazing regrows it. Spawns in groups of 2-4 on
// plains/forest/tundra grass. Model follows Minecraft's SheepModel +
// SheepFurModel (16 px = 1 block), facing +Z.
class Sheep : public Mob {
  public:
    static constexpr float WANDER_SPEED = 0.08f; // blocks / tick (~1.6 b/s)
    static constexpr float PANIC_SPEED = 0.16f;

    Sheep();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    float eyeHeight() const override { return 1.23f; }

    // --- Wool ---
    WoolColor woolColor() const { return color; }
    void setWoolColor(WoolColor c) { color = c; }
    bool isSheared() const { return sheared; }
    // Remove the fleece. Returns false (no-op) if already sheared or dead.
    bool shear();
    void regrowWool() { sheared = false; }

    // Minecraft's natural spawn odds: white 81.836 %, light gray / gray /
    // black 5 % each, brown 3 %, pink 0.164 %.
    static WoolColor randomWoolColor(std::mt19937& rng);
    static glm::vec3 woolRgb(WoolColor c);

    // --- Grazing animation (driven by EatGrassGoal) ---
    int eatAnimTicks = 0; // counts down from EatGrassGoal::DURATION_TICKS
    // Head drop 0..1 and pitch (radians, + = face down) for the eat pose.
    float headEatDrop(float partialTick) const;
    float headEatPitch(float partialTick, float lookPitchRad) const;

  private:
    WoolColor color = WoolColor::White;
    bool sheared = false;
};
