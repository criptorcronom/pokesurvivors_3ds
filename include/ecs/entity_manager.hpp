#pragma once

#include "entity_types.hpp"
#include <cstring>

namespace PokeSurvivor::ECS {

class EntityManager {
public:
    PhysicsSoA physics;
    CombatSoA  combat;
    RenderSoA  render;
    AISoA      ai;

    uint32_t activeEnemies = 0;

    EntityManager() {
        reset();
    }

    void reset() {
        activeEnemies = 0;
        std::memset(&physics, 0, sizeof(physics));
        std::memset(&combat,  0, sizeof(combat));
        std::memset(&render,  0, sizeof(render));
        std::memset(&ai,      0, sizeof(ai));
    }

    // Spawns an enemy into the dense array in O(1)
    int32_t spawnEnemy(float x, float y, uint16_t dexId, int32_t hp, int16_t dmg, float spd, BehaviorType beh, float radius = 12.0f) {
        if (activeEnemies >= MAX_ENEMIES) {
            return -1; // Pool exhausted
        }

        uint32_t idx = activeEnemies++;

        // Initialize Physics (Hot)
        physics.posX[idx] = x;
        physics.posY[idx] = y;
        physics.velX[idx] = 0.0f;
        physics.velY[idx] = 0.0f;
        physics.radius[idx] = radius;
        physics.flags[idx] = FLAG_ACTIVE;

        // Initialize Combat (Warm)
        combat.hp[idx] = hp;
        combat.maxHp[idx] = hp;
        combat.baseDamage[idx] = dmg;
        combat.shieldHp[idx] = 0;
        combat.statusEffect[idx] = 0;
        combat.statusDuration[idx] = 0.0f;
        combat.statusTickTimer[idx] = 0.0f;
        combat.knockbackX[idx] = 0.0f;
        combat.knockbackY[idx] = 0.0f;
        combat.xpReward[idx] = 10;

        // Initialize Render (Render Hot)
        render.dexId[idx] = dexId;
        render.animState[idx] = 1; // Walk
        render.animFrame[idx] = 0;
        render.animTimer[idx] = 0.0f;
        render.scale[idx] = 1.0f;
        render.flashCounter[idx] = 0;

        // Initialize AI (Cold)
        ai.behavior[idx] = beh;
        ai.actionTimer[idx] = 0.0f;
        ai.cooldown[idx] = 0.0f;
        ai.targetAngle[idx] = 0.0f;
        ai.minionCount[idx] = 0;

        return static_cast<int32_t>(idx);
    }

    // O(1) Swap-and-Pop Removal keeps arrays dense without holes or pointer chasing
    void killEnemy(uint32_t idx) {
        if (idx >= activeEnemies) return;

        uint32_t lastIdx = activeEnemies - 1;

        if (idx != lastIdx) {
            // Swap physics
            physics.posX[idx] = physics.posX[lastIdx];
            physics.posY[idx] = physics.posY[lastIdx];
            physics.velX[idx] = physics.velX[lastIdx];
            physics.velY[idx] = physics.velY[lastIdx];
            physics.radius[idx] = physics.radius[lastIdx];
            physics.flags[idx] = physics.flags[lastIdx];

            // Swap combat
            combat.hp[idx] = combat.hp[lastIdx];
            combat.maxHp[idx] = combat.maxHp[lastIdx];
            combat.baseDamage[idx] = combat.baseDamage[lastIdx];
            combat.shieldHp[idx] = combat.shieldHp[lastIdx];
            combat.primaryType[idx] = combat.primaryType[lastIdx];
            combat.statusEffect[idx] = combat.statusEffect[lastIdx];
            combat.statusDuration[idx] = combat.statusDuration[lastIdx];
            combat.statusTickTimer[idx] = combat.statusTickTimer[lastIdx];
            combat.knockbackX[idx] = combat.knockbackX[lastIdx];
            combat.knockbackY[idx] = combat.knockbackY[lastIdx];
            combat.xpReward[idx] = combat.xpReward[lastIdx];

            // Swap render
            render.dexId[idx] = render.dexId[lastIdx];
            render.animState[idx] = render.animState[lastIdx];
            render.animFrame[idx] = render.animFrame[lastIdx];
            render.animTimer[idx] = render.animTimer[lastIdx];
            render.scale[idx] = render.scale[lastIdx];
            render.flashCounter[idx] = render.flashCounter[lastIdx];

            // Swap AI
            ai.behavior[idx] = ai.behavior[lastIdx];
            ai.actionTimer[idx] = ai.actionTimer[lastIdx];
            ai.cooldown[idx] = ai.cooldown[lastIdx];
            ai.targetAngle[idx] = ai.targetAngle[lastIdx];
            ai.minionCount[idx] = ai.minionCount[lastIdx];
        }

        activeEnemies--;
    }
};

} // namespace PokeSurvivor::ECS
