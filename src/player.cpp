#include "player.h"
#include "melee.h"
#include "mining.h"
#include "entity_manager.h"
#include "collision.h"
#include "mob_manager.h"
#include "mobs/sheep.h"
#include "net/net_session.h"
#include "particle_system.h"
#include "world.h"
#include "ChunkManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

Player::Player() {}

Player::~Player() = default;

void Player::destroyAudio() {
    if (stepSoundsLoaded) {
        for (auto& set : stepSounds)
            for (auto& s : set.sounds) ma_sound_uninit(&s);
        for (auto& set : breakSounds)
            for (auto& s : set.sounds) ma_sound_uninit(&s);
        ma_sound_uninit(&underwaterAmbience);
        for (auto& s : enterSounds) ma_sound_uninit(&s);
        for (auto& s : exitSounds) ma_sound_uninit(&s);
        for (auto& s : bubbleSounds) ma_sound_uninit(&s);
        if (pickupSoundLoaded) ma_sound_uninit(&pickupSound);
        pickupSoundLoaded = false;
        stepSoundsLoaded = false;
    }
}

void Player::initAudio(ma_engine* engine) {
    audioEngine = engine;
    if (!engine) return;

    struct SoundDef {
        StepSound type;
        const char* prefix;
        int count;
    };
    SoundDef defs[] = {
        {STEP_GRASS, "assets/Sounds/step/grass/grass", 6}, {STEP_STONE, "assets/Sounds/step/stone/stone", 6},
        {STEP_SAND, "assets/Sounds/step/sand/sand", 5},    {STEP_GRAVEL, "assets/Sounds/step/gravel/gravel", 4},
        {STEP_SNOW, "assets/Sounds/step/snow/snow", 4},    {STEP_WOOD, "assets/Sounds/step/wood/wood", 6},
        {STEP_WATER, "assets/Sounds/step/water/swim", 4},  {STEP_CLOTH, "assets/Sounds/dig/cloth/cloth", 4},
    };

    for (auto& def : defs) {
        auto& set = stepSounds[def.type - 1];
        set.sounds.resize(def.count);
        for (int i = 0; i < def.count; i++) {
            std::string path = std::string(def.prefix) + std::to_string(i + 1) + ".ogg";
            ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &set.sounds[i]);
        }
    }
    // Load break/dig sounds (same categories, different files)
    SoundDef breakDefs[] = {
        {STEP_GRASS, "assets/Sounds/dig/grass/grass", 4}, {STEP_STONE, "assets/Sounds/dig/stone/stone", 4},
        {STEP_SAND, "assets/Sounds/dig/sand/sand", 4},    {STEP_GRAVEL, "assets/Sounds/dig/gravel/gravel", 4},
        {STEP_SNOW, "assets/Sounds/dig/snow/snow", 4},    {STEP_WOOD, "assets/Sounds/dig/wood/wood", 4},
        {STEP_CLOTH, "assets/Sounds/dig/cloth/cloth", 4},
    };
    for (auto& def : breakDefs) {
        auto& set = breakSounds[def.type - 1];
        set.sounds.resize(def.count);
        for (int i = 0; i < def.count; i++) {
            std::string path = std::string(def.prefix) + std::to_string(i + 1) + ".ogg";
            ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &set.sounds[i]);
        }
    }

    // Underwater sounds
    ma_sound_init_from_file(engine, "assets/Sounds/ambient/underwater/underwater_ambience.ogg", MA_SOUND_FLAG_DECODE,
                            nullptr, nullptr, &underwaterAmbience);
    ma_sound_set_looping(&underwaterAmbience, MA_TRUE);
    ma_sound_set_volume(&underwaterAmbience, 0.4f);
    for (int i = 0; i < 3; i++) {
        std::string path = "assets/Sounds/ambient/underwater/enter" + std::to_string(i + 1) + ".ogg";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &enterSounds[i]);
    }
    for (int i = 0; i < 3; i++) {
        std::string path = "assets/Sounds/ambient/underwater/exit" + std::to_string(i + 1) + ".ogg";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &exitSounds[i]);
    }
    for (int i = 0; i < 6; i++) {
        std::string path = "assets/Sounds/ambient/underwater/bubbles" + std::to_string(i + 1) + ".ogg";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &bubbleSounds[i]);
        ma_sound_set_volume(&bubbleSounds[i], 0.3f);
    }

    // Item pickup "pop" (generated for this game, not Minecraft's).
    pickupSoundLoaded = ma_sound_init_from_file(engine, "assets/Sounds/random/pop.ogg", MA_SOUND_FLAG_DECODE,
                                                nullptr, nullptr, &pickupSound) == MA_SUCCESS;
    if (pickupSoundLoaded) ma_sound_set_volume(&pickupSound, 0.6f);

    stepSoundsLoaded = true;
    lastStepPos = camera.getPosition();
}

void Player::playStepSound(block_type groundBlock) {
    StepSound ss = getStepSound(groundBlock);
    if (ss == STEP_NONE || !stepSoundsLoaded) return;

    auto& set = stepSounds[ss - 1];
    if (set.sounds.empty()) return;

    static std::mt19937 rng(42);
    int idx = std::uniform_int_distribution<int>(0, (int)set.sounds.size() - 1)(rng);
    ma_sound_seek_to_pcm_frame(&set.sounds[idx], 0);
    ma_sound_start(&set.sounds[idx]);
}

void Player::playBreakSound(block_type brokenBlock) {
    StepSound ss = getStepSound(brokenBlock);
    if (ss == STEP_NONE || ss == STEP_WATER || !stepSoundsLoaded) return;
    if (ss - 1 >= NUM_SOUND_TYPES) return;

    auto& set = breakSounds[ss - 1];
    if (set.sounds.empty()) return;

    static std::mt19937 rng(123);
    int idx = std::uniform_int_distribution<int>(0, (int)set.sounds.size() - 1)(rng);
    ma_sound_seek_to_pcm_frame(&set.sounds[idx], 0);
    ma_sound_start(&set.sounds[idx]);
}

void Player::handleInput(GLFWwindow* window, World* world) {
    // Movement
    bool wDown = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    if (wDown) camera.forward();
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.back();
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.left();
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.right();

    // Double-tap W → latch sprint until the player releases forward.
    if (wDown && !wWasPressed) {
        double now = glfwGetTime();
        if (now - lastWTap < DOUBLE_TAP_TIME) sprintLatched = true;
        lastWTap = now;
    }
    if (!wDown) sprintLatched = false;
    wWasPressed = wDown;

    // Space: double-tap toggles walk/fly. Holding space keeps jumping on
    // land (camera.jump() gates on onGround, so it only fires when grounded)
    // and keeps pushing up in fly / water.
    {
        bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasPressed) {
            double now = glfwGetTime();
            if (now - lastSpaceTap < DOUBLE_TAP_TIME && !isSurvival()) {
                camera.toggleWalkMode();
                lastSpaceTap = 0;
            } else {
                lastSpaceTap = now;
            }
        }
        if (spaceDown) {
            if (camera.isWalkMode()) {
                if (isSurvival() && camera.isOnGround() && !camera.isInWater())
                    hunger.addExhaustion(sprinting ? PlayerHunger::EXHAUST_SPRINT_JUMP : PlayerHunger::EXHAUST_JUMP);
                camera.jump(); // onGround-gated, so auto-repeats on landing
            }
            else
                camera.up();
        }
        spaceWasPressed = spaceDown;
    }

    // Down (fly mode only) — Shift or Q
    bool qDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
    if (!camera.isWalkMode() && (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || qDown)) camera.down();
    // Survival never flies, so Q is free for Minecraft's "drop item" (with
    // Ctrl: the whole stack).
    if (isSurvival() && qDown && !qWasPressed) {
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        dropSelected(world, ctrl);
    }
    qWasPressed = qDown;

    // Sprint — Shift in walk mode, R always, or double-tap W latch.
    bool sprint = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS || sprintLatched;
    if (camera.isWalkMode()) sprint = sprint || glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (isSurvival() && !hunger.canSprint()) sprint = false; // too hungry
    sprinting = sprint;
    if (sprint)
        camera.speedUp();
    else
        camera.resetSpeed();

    // Hotbar selection: keys 1-9 = slots 0-8, key 0 = slot 9
    for (int i = 0; i < HOTBAR_SIZE; i++) {
        int key = (i < 9) ? (GLFW_KEY_1 + i) : GLFW_KEY_0;
        if (glfwGetKey(window, key) == GLFW_PRESS) selectedSlot = i;
    }

    // Left click: punch a mob, else break the targeted block — instantly in
    // Creative, by holding the button in Survival (updateMining).
    bool leftDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    double now = glfwGetTime();
    if (leftDown && !leftClickHeld && world) {
        isPunching = true;
        punchStartTime = now;
        float strength =
            melee::attackStrength(punchStartTime - lastSwingTime, meleeStats(getSelectedItem()).cooldownSeconds);
        lastSwingTime = punchStartTime;
        // Mobs are tested before blocks: a punch that lands on a mob never
        // also breaks the block behind it.
        miningBlocked = tryAttack(world, strength);
        if (!miningBlocked && hasHighlight && !isSurvival()) {
            Cube* block = world->getBlock(targeted.x, targeted.y, targeted.z);
            if (block) breakTargeted(world, block->getType(), getSelectedItem());
        }
    }
    if (isSurvival()) updateMining(world, leftDown && !miningBlocked && hasHighlight, now);
    leftClickHeld = leftDown;

    // Right click: interact with a mob (shear) if one is in front, else
    // ignite TNT if targeted, otherwise place block
    bool rightDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    if (isSurvival()) updateEating(rightDown, now);
    bool rightPressed = rightDown && !rightClickHeld && world && !eating;
    if (rightPressed && tryInteractMob(world)) {
        isPunching = true;
        punchStartTime = glfwGetTime();
    } else if (rightPressed && hasHighlight) {
        Cube* targetBlock = world->getBlock(targeted.x, targeted.y, targeted.z);
        bool sneaking = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        if (targetBlock && targetBlock->getType() == TNT) {
            world->igniteTnt(targeted, glfwGetTime());
            isPunching = true;
            punchStartTime = glfwGetTime();
        } else if (targetBlock && targetBlock->getType() == CRAFTING_TABLE && !sneaking) {
            // Shift-click still places against the table, as in Minecraft.
            craftingTableRequested = true;
        } else if (getSelectedBlockType() != AIR) {
            glm::vec3 camPos = camera.getPosition();
            int px = (int)std::floor(camPos.x);
            int pzCoord = (int)std::floor(camPos.z);
            int pyHead = (int)std::floor(camPos.y);
            int pyFeet = (int)std::floor(camPos.y - PLAYER_HEIGHT);
            // Don't place inside player body (2 blocks tall)
            bool blocked = (placementPos.x == px && placementPos.z == pzCoord &&
                            (placementPos.y == pyHead || placementPos.y == pyFeet));
            if (!blocked) {
                playBreakSound(getSelectedBlockType());
                // Client-side prediction: apply locally, then send the
                // intent. The host's PlaceApply echo will idempotently
                // re-apply the same blockType, so the prediction is
                // transparent on the happy path.
                if (netSession)
                    netSession->notifyLocalPlace(placementPos, static_cast<uint8_t>(getSelectedBlockType()));
                block_type placed = getSelectedBlockType();
                world->placeBlock(placementPos, placed);
                if (isSurvival()) inventory.take(selectedSlot, 1); // Creative never runs out
                // Arm swing (same feedback as left-click) + outline pop at placement pos
                isPunching = true;
                punchStartTime = glfwGetTime();
                lastPlacedPos = placementPos;
                placeAnimStart = glfwGetTime();
            }
        }
    }
    rightClickHeld = rightDown;
}

void Player::breakTargeted(World* world, block_type type, ItemId tool) {
    playBreakSound(type);
    // Optimistic local apply — the host's DestroyApply echo will re-run
    // setBlock with the same AIR result (idempotent), so prediction is
    // "free" until we add host-side rejects.
    if (netSession) netSession->notifyLocalDestroy(targeted);
    world->destroyBlock(glm::vec3(targeted));
    if (!isSurvival() || !world->entityManager) return;
    hunger.addExhaustion(PlayerHunger::EXHAUST_BREAK_BLOCK);
    static std::mt19937 dropRng(std::random_device{}());
    ItemDrop drop = blockDrop(type, tool, std::uniform_real_distribution<float>(0.0f, 1.0f)(dropRng));
    if (drop.count > 0) world->entityManager->dropFromBlock(drop.id, drop.count, targeted);
}

void Player::dropSelected(World* world, bool wholeStack) {
    ItemStack& stack = inventory.slots[selectedSlot];
    if (stack.empty() || !world || !world->entityManager) return;
    int n = wholeStack ? stack.count : 1;
    world->entityManager->throwItem(stack.id, n, camera.getPosition(), camera.getFront());
    inventory.take(selectedSlot, n);
}

void Player::setVitals(float healthValue, int food, float saturation) {
    health.health = std::clamp(healthValue, 1.0f, PlayerHealth::MAX_HEALTH);
    hunger.food = std::clamp(food, 0, PlayerHunger::MAX_FOOD);
    hunger.saturation = std::clamp(saturation, 0.0f, (float)hunger.food);
}

void Player::updateHunger(double now) {
    double dt = lastUpdateTime < 0.0 ? 0.0 : std::min(now - lastUpdateTime, 0.25);
    lastUpdateTime = now;
    if (health.isDead()) return;

    // Moving costs: swimming counts every direction, sprinting on land
    // only the horizontal distance (walking is free).
    glm::vec3 p = camera.getPosition();
    float moved3d = glm::length(p - lastExhaustPos);
    float movedFlat = glm::length(glm::vec2(p.x - lastExhaustPos.x, p.z - lastExhaustPos.z));
    lastExhaustPos = p;
    if (moved3d < 2.0f) { // ignore teleports / respawns
        if (camera.isInWater())
            hunger.addExhaustion(moved3d * PlayerHunger::EXHAUST_SWIM_PER_BLOCK);
        else if (sprinting && camera.isOnGround())
            hunger.addExhaustion(movedFlat * PlayerHunger::EXHAUST_SPRINT_PER_BLOCK);
    }

    hungerAccumulator += dt;
    while (hungerAccumulator >= 0.05) {
        hungerAccumulator -= 0.05;
        float change = hunger.tick(health.health, PlayerHealth::MAX_HEALTH);
        if (change > 0.0f) health.heal(change);
        if (change < 0.0f) hurt(-change, p, false, false); // starving
    }
}

void Player::updateHazards(World* world, double dt) {
    // Which liquid the feet / chest / eye blocks are in (the blocks
    // Camera::update tests), read now. The camera's own in-liquid flags
    // date from its last update and can't tell water from lava, so water
    // splashes and bubbles go by these instead.
    auto typeAt = [&](int x, int y, int z) {
        Cube* c = world ? world->getBlock(x, y, z) : nullptr;
        return c ? c->getType() : AIR;
    };
    glm::vec3 eye = camera.getPosition();
    int fx = (int)std::floor(eye.x + 0.5f), fz = (int)std::floor(eye.z + 0.5f);
    int feetY = (int)std::floor(eye.y - PLAYER_HEIGHT + 0.5f);
    block_type eyeT = typeAt(fx, (int)std::floor(eye.y + 0.5f), fz);
    block_type feetT = typeAt(fx, feetY, fz), chestT = typeAt(fx, feetY + 1, fz);
    eyesInLava_ = eyeT == LAVA;
    bodyInLava = eyesInLava_ || feetT == LAVA || chestT == LAVA;
    eyesInWater_ = eyeT == WATER;
    bodyInWater = !bodyInLava && (eyesInWater_ || feetT == WATER || chestT == WATER);
    if (!isSurvival() || health.isDead()) {
        fireTicks = 0;
        return;
    }
    hazardAccumulator += dt;
    while (hazardAccumulator >= 0.05) {
        hazardAccumulator -= 0.05;
        if (bodyInLava) {
            hurt(LAVA_DAMAGE, eye, false); // the 0.5 s invulnerability paces it
            fireTicks = std::max(fireTicks, LAVA_FIRE_TICKS);
        } else if (bodyInWater) {
            fireTicks = 0; // water puts the fire out
        } else if (fireTicks > 0) {
            if (fireTicks % 20 == 0) hurt(1.0f, eye, false);
            --fireTicks;
        }
    }
}

void Player::updateEating(bool rightDown, double now) {
    ItemId held = getSelectedItem();
    bool canStart = rightDown && isFood(held) && hunger.canEat();
    if (eating && (!rightDown || selectedSlot != eatSlot || held != eatItem)) eating = false; // let go / switched
    if (!eating) {
        if (!canStart || rightClickHeld) return; // a fresh press starts eating
        eating = true;
        eatStart = now;
        eatSlot = selectedSlot;
        eatItem = held;
    }
    // Chewing: the hand bobs a few times a second.
    if (!isPunching || now - punchStartTime > 0.25) {
        isPunching = true;
        punchStartTime = now;
    }
    if (now - eatStart < EAT_SECONDS) return;
    hunger.eat(foodStats(eatItem));
    inventory.take(eatSlot, 1);
    // Still holding the button with food left: start on the next one.
    eatStart = now;
    if (!hunger.canEat() || getSelectedItem() != eatItem) eating = false;
}

float Player::getEatProgress() const {
    return eating ? std::clamp((float)((glfwGetTime() - eatStart) / EAT_SECONDS), 0.0f, 1.0f) : -1.0f;
}

void Player::spillInventory(World* world) {
    if (world && world->entityManager) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f), speed(0.1f, 0.6f);
        for (const ItemStack& stack : inventory.slots) {
            if (stack.empty()) continue;
            float a = angle(rng), s = speed(rng);
            world->entityManager->throwItem(stack.id, stack.count, camera.getPosition(),
                                            glm::vec3(std::cos(a) * s, 0.2f, std::sin(a) * s));
        }
    }
    inventory.clear();
}

void Player::updateMining(World* world, bool active, double now) {
    Cube* block = active && world ? world->getBlock(targeted.x, targeted.y, targeted.z) : nullptr;
    if (!block || block->getType() == AIR) {
        mining = false;
        return;
    }
    block_type type = block->getType();
    ItemId tool = getSelectedItem();
    // Looking at another block or switching item starts over, as in Minecraft.
    if (!mining || targeted != miningPos || type != miningType || tool != miningTool) {
        mining = true;
        miningPos = targeted;
        miningType = type;
        miningTool = tool;
        miningTicks = breakTicks(type, tool);
        miningProgress = 0.0;
        lastMiningTime = now;
    }
    if (miningTicks < 0) return; // unbreakable (bedrock)
    double dt = std::min(now - lastMiningTime, 0.1); // no jump after a stall
    lastMiningTime = now;
    if (now < breakCooldownUntil) return;

    // Keep swinging the arm and tapping the block's sound while digging.
    if (!isPunching) {
        isPunching = true;
        punchStartTime = now;
    }
    if (now >= nextMiningSound) {
        playStepSound(type);
        nextMiningSound = now + 0.2; // every 4 ticks
    }

    miningProgress += miningTicks == 0 ? 1.0 : dt * 20.0 / miningTicks;
    if (miningProgress < 1.0) return;
    breakTargeted(world, type, tool);
    mining = false;
    breakCooldownUntil = now + 0.25;
}

bool Player::tryAttack(World* world, float strength) {
    glm::vec3 eye = camera.getPosition();
    glm::vec3 dir = camera.getFront();
    constexpr float NONE = std::numeric_limits<float>::max();

    float mobDist = NONE; // raycast only writes it on a hit
    const float reach = attackReach(gameMode);
    Mob* mob = world->mobManager ? world->mobManager->raycast(eye, dir, reach, mobDist) : nullptr;

    // Other players, hit-tested where we see them.
    double now = glfwGetTime();
    float remoteDist = NONE;
    const RemotePlayer* remote = netSession && netSession->connected()
                                     ? netSession->raycastRemote(eye, dir, reach, now, remoteDist)
                                     : nullptr;

    float targetDist = std::min(mobDist, remoteDist);
    if (targetDist == NONE) return false;
    // A targeted block nearer than the target shields it.
    if (hasHighlight) {
        float blockDist = 0.0f;
        glm::vec3 b(targeted);
        if (rayIntersectsAABB(eye, dir, b - glm::vec3(0.5f), b + glm::vec3(0.5f), blockReach(gameMode), blockDist) &&
            blockDist < targetDist)
            return false;
    }
    // Minecraft-style critical: a charged hit while falling.
    bool falling = camera.isWalkMode() && !camera.isOnGround() && !camera.isInWater() && camera.getVelocityY() < 0.0f;
    float dmg = melee::damage(meleeStats(getSelectedItem()).damage, strength, falling);
    if (isSurvival()) hunger.addExhaustion(PlayerHunger::EXHAUST_ATTACK);
    if (remote && remoteDist < mobDist) {
        // Swings inside their invulnerability window (or at a dead player)
        // send nothing and give no feedback — only real hits sound.
        if (netSession->sendHit(remote->peerId, dmg, eye, now)) playHitSound();
    } else if (world->mobManager->attackMob(*mob, dmg, eye)) {
        playHitSound();
    }
    return true;
}

bool Player::tryInteractMob(World* world) {
    if (!world->mobManager) return false;
    glm::vec3 eye = camera.getPosition();
    glm::vec3 dir = camera.getFront();
    float mobDist = 0.0f;
    Mob* mob = world->mobManager->raycast(eye, dir, attackReach(gameMode), mobDist);
    if (!mob || mob->type != MobType::Sheep) return false;
    // A targeted block nearer than the mob shields it.
    if (hasHighlight) {
        float blockDist = 0.0f;
        glm::vec3 b(targeted);
        if (rayIntersectsAABB(eye, dir, b - glm::vec3(0.5f), b + glm::vec3(0.5f), blockReach(gameMode), blockDist) &&
            blockDist < mobDist)
            return false;
    }
    // Already-sheared sheep fall through to normal block placement.
    return static_cast<Sheep*>(mob)->shear();
}

void Player::playPickupSound() {
    if (!pickupSoundLoaded) return;
    // A different pitch each time, as Minecraft does for its pop.
    static std::mt19937 rng(91);
    ma_sound_set_pitch(&pickupSound, std::uniform_real_distribution<float>(0.8f, 1.6f)(rng));
    ma_sound_seek_to_pcm_frame(&pickupSound, 0);
    ma_sound_start(&pickupSound);
}

void Player::playHitSound() {
    // No dedicated hit samples in assets — the cloth dig sound reads as a
    // soft thump. Silent if audio isn't loaded.
    if (!stepSoundsLoaded) return;
    auto& set = breakSounds[STEP_CLOTH - 1];
    if (set.sounds.empty()) return;
    static std::mt19937 rng(77);
    int idx = std::uniform_int_distribution<int>(0, (int)set.sounds.size() - 1)(rng);
    ma_sound_seek_to_pcm_frame(&set.sounds[idx], 0);
    ma_sound_start(&set.sounds[idx]);
}

void Player::setGameMode(GameMode mode) {
    gameMode = mode;
    if (isSurvival()) camera.setWalkMode(true);
}

void Player::hurt(float amount, glm::vec3 fromPos, bool knockback, bool exhausting) {
    if (!isSurvival() || !health.hurt(amount, glfwGetTime())) return;
    if (exhausting) hunger.addExhaustion(PlayerHunger::EXHAUST_HURT);
    if (knockback) {
        glm::vec3 p = camera.getPosition();
        glm::vec2 away(p.x - fromPos.x, p.z - fromPos.z);
        float len = glm::length(away);
        if (len > 0.001f) camera.applyKnockback(away / len * HURT_KNOCKBACK, 0.12f);
    }
    playHitSound();
}

void Player::respawn(glm::vec3 eyePos) {
    health.respawn(glfwGetTime());
    hunger.reset();
    eating = false;
    fireTicks = 0;
    camera.setPosition(eyePos);
    camera.resetMotion();
}

float Player::getHurtFlash() const {
    return health.hurtFlash(glfwGetTime());
}

void Player::updateMouseLook(double xPos, double yPos, int /*windowWidth*/, int /*windowHeight*/) {
    static double lastX = 0, lastY = 0;
    static bool initialized = false;
    if (firstMouse || !initialized) {
        lastX = xPos;
        lastY = yPos;
        firstMouse = false;
        initialized = true;
        return;
    }

    float sensitivity = 0.1f * mouseSensitivity;
    float xOffset = (float)(xPos - lastX) * sensitivity;
    float yOffset = (float)(lastY - yPos) * sensitivity;
    lastX = xPos;
    lastY = yPos;

    yaw += xOffset;
    pitch += yOffset;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camera.changeDirection(direction);
}

void Player::update(World* world) {
    // Update punch animation
    if (isPunching && (glfwGetTime() - punchStartTime) > PUNCH_DURATION) isPunching = false;

    // Survival never flies: a respawn into an unloaded spawn chunk hovers
    // (main.cpp) only until the chunk under the player has streamed in, and
    // main.cpp puts the player on the ground first.
    if (isSurvival() && !camera.isWalkMode() && world) {
        glm::vec3 p = camera.getPosition();
        int cx = worldToChunk((int)std::floor(p.x + 0.5f));
        int cz = worldToChunk((int)std::floor(p.z + 0.5f));
        if (world->chunkManager->getChunk(cx, cz)) camera.setWalkMode(true);
    }

    // Physics
    findGroundAndUpdate(world);

    // Health: fall damage on landing (walk mode only — Camera doesn't track
    // falls while flying or in water), then slow regeneration.
    float fallDmg = PlayerHealth::fallDamage(camera.takeLandingFall());
    if (fallDmg > 0.0f) hurt(fallDmg, camera.getPosition(), false, false);
    // Survival heals (and starves) through the food bar; Creative can't be
    // hurt, so its timer-based regen never matters there.
    double hazardDt = lastUpdateTime < 0.0 ? 0.0 : std::min(glfwGetTime() - lastUpdateTime, 0.25);
    if (isSurvival())
        updateHunger(glfwGetTime());
    else
        health.update(glfwGetTime());
    updateHazards(world, hazardDt);

    // Movement sounds: swim sounds when any body part in water, footsteps on land
    // Splashes, bubbles and underwater ambience are water's, not lava's.
    bool inWater = bodyInWater;
    bool eyesUnder = eyesInWater_;
    bool moving = camera.isWalkMode() && (inWater || camera.isOnGround());
    if (stepSoundsLoaded && moving) {
        glm::vec3 pos = camera.getPosition();
        float dx = pos.x - lastStepPos.x;
        float dz = pos.z - lastStepPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > 0.001f) {
            distSinceStep += std::sqrt(distSq);
            lastStepPos = pos;
        }

        float interval = inWater ? STEP_INTERVAL * 3.0f : STEP_INTERVAL;
        if (distSinceStep >= interval) {
            distSinceStep = 0.0f;
            playStepSound(inWater ? WATER : groundBlockType);
        }
    }

    // Underwater audio: splash on enter/exit (any body part), bubbles+ambient only when eyes under
    static std::mt19937 waterRng(42);
    if (stepSoundsLoaded) {
        if (inWater && !wasSubmerged) {
            int idx = waterRng() % 3;
            ma_sound_seek_to_pcm_frame(&enterSounds[idx], 0);
            ma_sound_start(&enterSounds[idx]);
        } else if (!inWater && wasSubmerged) {
            int idx = waterRng() % 3;
            ma_sound_seek_to_pcm_frame(&exitSounds[idx], 0);
            ma_sound_start(&exitSounds[idx]);
        }
        if (eyesUnder && !wasEyesUnder) {
            ma_sound_seek_to_pcm_frame(&underwaterAmbience, 0);
            ma_sound_start(&underwaterAmbience);
        } else if (!eyesUnder && wasEyesUnder) {
            ma_sound_stop(&underwaterAmbience);
        }
        if (eyesUnder) {
            double now = glfwGetTime();
            if (now - lastBubbleTime > 3.0 + (std::fmod(now * 7.3, 4.0))) {
                lastBubbleTime = now;
                int idx = waterRng() % 6;
                ma_sound_seek_to_pcm_frame(&bubbleSounds[idx], 0);
                ma_sound_start(&bubbleSounds[idx]);
            }
        }
        // Splash bubbles only when first entering water — not a continuous
        // stream. Underwater motes are handled in the main render loop so
        // they benefit from camera front direction.
        if (inWater && !wasSubmerged && world && world->particles) {
            glm::vec3 chest = camera.getPosition() - glm::vec3(0.0f, PLAYER_HEIGHT * 0.5f, 0.0f);
            world->particles->spawnBubbles(chest, 18);
        }
        wasSubmerged = inWater;
        wasEyesUnder = eyesUnder;
    }

    // Block targeting
    updateTargetedBlock(world);
}

void Player::findGroundAndUpdate(World* world) {
    auto blockCheck = [](int bx, int by, int bz, void* ctx) -> bool {
        auto* cm = static_cast<ChunkManager*>(ctx);
        int cx = worldToChunk(bx);
        int cz = worldToChunk(bz);
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        block_type bt = chunk->getBlockType(worldToLocal(bx, cx), by, worldToLocal(bz, cz));
        return hasFlag(bt, BF_SOLID);
    };

    auto waterCheck = [](int bx, int by, int bz, void* ctx) -> bool {
        auto* cm = static_cast<ChunkManager*>(ctx);
        int cx = worldToChunk(bx);
        int cz = worldToChunk(bz);
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        // Lava swims like (thick) water; only water has a flow push.
        return hasFlag(chunk->getBlockType(worldToLocal(bx, cx), by, worldToLocal(bz, cz)), BF_LIQUID);
    };

    // Flow vector at a water block: (fx, fy, fz). Horizontal from level
    // gradient; Y = -1 for falling water (pushes player downward).
    auto flowCheck = [](int bx, int by, int bz, void* ctx) -> glm::vec3 {
        constexpr float FLOW_INTO_AIR_WEIGHT = 8.0f;
        auto* cm = static_cast<ChunkManager*>(ctx);
        // Sample one cell: returns {blockType, waterRaw}. Chunk lookup is
        // cached across calls — player + 4 neighbors usually share a chunk.
        int lastCx = INT_MIN, lastCz = INT_MIN;
        Chunk* lastChunk = nullptr;
        auto sample = [&](int x, int y, int z) -> std::pair<block_type, uint8_t> {
            if (y < 0 || y >= CHUNK_HEIGHT) return {STONE, 0};
            int cx = worldToChunk(x), cz = worldToChunk(z);
            if (cx != lastCx || cz != lastCz) {
                lastCx = cx;
                lastCz = cz;
                lastChunk = cm->getChunk(cx, cz);
            }
            if (!lastChunk) return {STONE, 0};
            int lx = worldToLocal(x, cx), lz = worldToLocal(z, cz);
            block_type t = lastChunk->getBlockType(lx, y, lz);
            return {t, t == WATER ? lastChunk->getWaterLevel(lx, y, lz) : (uint8_t)0};
        };
        auto self = sample(bx, by, bz);
        if (self.first != WATER) return glm::vec3(0);
        if (waterIsFalling(self.second)) return glm::vec3(0, -1, 0);
        int myLvl = waterIsSource(self.second) ? 0 : waterFlowLevel(self.second);
        glm::vec3 flow(0);
        static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : dirs) {
            auto n = sample(bx + d[0], by, bz + d[1]);
            float contrib = 0;
            if (n.first == AIR) {
                contrib = FLOW_INTO_AIR_WEIGHT;
            } else if (n.first == WATER) {
                int nLvl = waterIsSource(n.second) ? 0 : waterFlowLevel(n.second);
                contrib = (float)(nLvl - myLvl);
            }
            flow.x += d[0] * contrib;
            flow.z += d[1] * contrib;
        }
        return flow;
    };

    camera.update(blockCheck, world->chunkManager, waterCheck, flowCheck);
    if (!camera.isWalkMode()) return;

    // Footstep sounds: look up block types at player feet. Block coords are
    // centered on integers (block N spans [N-0.5, N+0.5]), so we add +0.5
    // before flooring — same convention as camera.cpp's collision checks.
    // Without this shift, standing on a 1-block-thick surface reads the
    // block below (e.g. sand on dirt → wrong step sound).
    glm::vec3 pos = camera.getPosition();
    int bx = (int)std::floor(pos.x + 0.5f);
    int bz = (int)std::floor(pos.z + 0.5f);
    int cx = worldToChunk(bx);
    int cz = worldToChunk(bz);
    Chunk* chunk = world->chunkManager->getChunk(cx, cz);
    groundBlockType = AIR;
    feetBlockType = AIR;
    if (chunk) {
        int feetY = (int)std::floor(pos.y - PLAYER_HEIGHT + 0.5f);
        Cube* gb = chunk->getBlock(worldToLocal(bx, cx), feetY - 1, worldToLocal(bz, cz));
        if (gb) groundBlockType = gb->getType();
        Cube* fb = chunk->getBlock(worldToLocal(bx, cx), feetY, worldToLocal(bz, cz));
        if (fb) feetBlockType = fb->getType();
    }
}

void Player::updateTargetedBlock(World* world) {
    glm::vec3 pos = camera.getPosition();
    glm::vec3 front = camera.getFront();
    hasHighlight = world->raycast(pos, front, blockReach(gameMode), targeted, placementPos);
}

float Player::getSwingProgress() const {
    if (!isPunching) return 0.0f;
    return std::clamp((float)((glfwGetTime() - punchStartTime) / PUNCH_DURATION), 0.0f, 1.0f);
}

float Player::getPunchSwingAngle() const {
    if (!isPunching) return 0.0f;
    float t = (float)((glfwGetTime() - punchStartTime) / PUNCH_DURATION);
    return std::sin(t * glm::radians(180.0f)) * -60.0f;
}

float Player::getPlaceAnimProgress() const {
    if (placeAnimStart < 0) return -1.0f;
    double t = (glfwGetTime() - placeAnimStart) / PLACE_ANIM_DURATION;
    if (t >= 1.0) return -1.0f; // done
    return (float)t;
}

glm::mat4 Player::getArmModelMatrix() const {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(0.4f, -0.4f, -0.6f));
    m = glm::rotate(m, glm::radians(getPunchSwingAngle()), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-10.0f), glm::vec3(0, 0, 1));
    return m;
}
