#pragma once

// Survival hunger, following Minecraft Java's FoodData: activity adds
// exhaustion; every 4 points of it burn one point of saturation, or of
// food once saturation is gone. Food drives health: with a full bar,
// spare saturation heals quickly; at 18+ food health regenerates slowly;
// at 0 the player starves (down to 1 HP, as on Normal difficulty). Pure
// logic at 20 ticks per second, so it is unit-tested.

#include <algorithm>

#include "item.h"

class PlayerHunger {
  public:
    static constexpr int MAX_FOOD = 20; // half drumsticks
    static constexpr float START_SATURATION = 5.0f;
    static constexpr float MAX_EXHAUSTION = 40.0f;
    static constexpr int SPRINT_MIN_FOOD = 7; // can't sprint at 6 or less

    // Exhaustion per action (Minecraft values).
    static constexpr float EXHAUST_SPRINT_PER_BLOCK = 0.1f;
    static constexpr float EXHAUST_SWIM_PER_BLOCK = 0.01f; // 3D distance
    static constexpr float EXHAUST_JUMP = 0.05f;
    static constexpr float EXHAUST_SPRINT_JUMP = 0.2f;
    static constexpr float EXHAUST_ATTACK = 0.1f;
    static constexpr float EXHAUST_HURT = 0.1f; // hits/explosions, not falls or starving
    static constexpr float EXHAUST_BREAK_BLOCK = 0.005f;

    int food = MAX_FOOD;
    float saturation = START_SATURATION;
    float exhaustion = 0.0f;

    void addExhaustion(float amount) { exhaustion = std::min(exhaustion + amount, MAX_EXHAUSTION); }

    bool canEat() const { return food < MAX_FOOD; }
    bool canSprint() const { return food >= SPRINT_MIN_FOOD; }

    // Saturation never exceeds the food level.
    void eat(FoodStats f) {
        food = std::min(food + f.hunger, MAX_FOOD);
        saturation = std::min(saturation + f.saturation, static_cast<float>(food));
    }

    // One tick. Returns the health change it causes: + healing, − hunger
    // damage (the caller applies it, so damage goes through the usual hurt
    // path). `health`/`maxHealth` in half-hearts.
    float tick(float health, float maxHealth) {
        if (exhaustion > 4.0f) {
            exhaustion -= 4.0f;
            if (saturation > 0.0f)
                saturation = std::max(saturation - 1.0f, 0.0f);
            else
                food = std::max(food - 1, 0);
        }
        bool hurt = health < maxHealth;
        if (saturation > 0.0f && food >= MAX_FOOD && hurt) {
            // Fast regen while saturated: up to 1 HP per half second.
            if (++timer >= 10) {
                timer = 0;
                float spent = std::min(saturation, 6.0f);
                addExhaustion(spent);
                return spent / 6.0f;
            }
        } else if (food >= 18 && hurt) {
            if (++timer >= 80) {
                timer = 0;
                addExhaustion(6.0f);
                return 1.0f;
            }
        } else if (food <= 0) {
            if (++timer >= 80) {
                timer = 0;
                if (health > 1.0f) return -1.0f;
            }
        } else {
            timer = 0;
        }
        return 0.0f;
    }

    void reset() {
        food = MAX_FOOD;
        saturation = START_SATURATION;
        exhaustion = 0.0f;
        timer = 0;
    }

  private:
    int timer = 0;
};
