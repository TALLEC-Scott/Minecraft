#pragma once

// Being on fire, shared by the player and mobs (Minecraft's
// remainingFireTicks): lava does LAVA_DAMAGE per hit (paced by the
// victim's invulnerability) and sets you alight for LAVA_FIRE_TICKS;
// burning deals BURN_DAMAGE every BURN_INTERVAL ticks, starting the tick
// it catches; water puts it out. All in 20 TPS ticks. Pure, so tested
// through Player-free and Mob tests alike.

#include <algorithm>

struct FireState {
    static constexpr float LAVA_DAMAGE = 4.0f;
    static constexpr int LAVA_FIRE_TICKS = 300; // 15 s
    static constexpr int BURN_INTERVAL = 20;    // ticks between fire damage
    static constexpr float BURN_DAMAGE = 1.0f;

    int ticks = 0;     // > 0 while on fire (counts down)
    int burnTimer = 0; // ticks until the next fire damage

    bool burning() const { return ticks > 0; }
    void ignite(int forTicks) { ticks = std::max(ticks, forTicks); }
    void extinguish() {
        ticks = 0;
        burnTimer = 0;
    }
    // One tick of burning; returns the fire damage to deal this tick.
    float tick() {
        if (ticks <= 0) {
            burnTimer = 0;
            return 0.0f;
        }
        float dmg = 0.0f;
        if (burnTimer <= 0) {
            dmg = BURN_DAMAGE;
            burnTimer = BURN_INTERVAL;
        }
        --burnTimer;
        --ticks;
        return dmg;
    }
};
