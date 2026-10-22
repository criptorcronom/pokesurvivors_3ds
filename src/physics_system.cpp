#include "physics/physics_system.hpp"
#include "audio/audio_manager.hpp"
#include <cmath>

namespace PokeSurvivor::Physics {

void PhysicsSystem::RebuildSpatialGrid(
    Spatial::SpatialHashGrid& grid,
    const ECS::PhysicsSoA& phys,
    uint32_t activeCount
) {
    grid.clear();

    for (uint32_t i = 0; i < activeCount; ++i) {
        if (phys.flags[i] & ECS::FLAG_ACTIVE) {
            grid.insert(phys.posX[i], phys.posY[i], static_cast<uint16_t>(i));
        }
    }
}

void PhysicsSystem::UpdateSteering(
    ECS::PhysicsSoA& phys,
    const ECS::AISoA& ai,
    const ECS::PlayerData& player,
    uint32_t activeCount,
    float dt
) {
    (void)dt; // dt reserved for lerp / acceleration smoothing if needed

    for (uint32_t i = 0; i < activeCount; ++i) {
        if (!(phys.flags[i] & ECS::FLAG_ACTIVE) || (phys.flags[i] & ECS::FLAG_STUNNED)) {
            phys.velX[i] = 0.0f;
            phys.velY[i] = 0.0f;
            continue;
        }

        float diffX = player.x - phys.posX[i];
        float diffY = player.y - phys.posY[i];
        float distSq = diffX * diffX + diffY * diffY;

        // Facing direction bitflag update (branchless mask update)
        if (diffX < -0.1f) {
            phys.flags[i] |= ECS::FLAG_FACING_LEFT;
        } else if (diffX > 0.1f) {
            phys.flags[i] &= ~ECS::FLAG_FACING_LEFT;
        }

        if (distSq < 1e-4f) {
            phys.velX[i] = 0.0f;
            phys.velY[i] = 0.0f;
            continue;
        }

        float invDist = 1.0f / sqrtf(distSq);
        float dirX = diffX * invDist;
        float dirY = diffY * invDist;

        // Base archetype speeds
        float speed = 38.0f; // Default chaser speed in pixels/sec

        ECS::BehaviorType beh = ai.behavior[i];
        if (beh == ECS::BehaviorType::CHARGER) {
            speed = 52.0f;
        } else if (beh == ECS::BehaviorType::RANGED) {
            constexpr float PREFERRED_DIST = 160.0f;
            float currentDist = distSq * invDist;

            if (currentDist < PREFERRED_DIST * 0.75f) {
                // Back off away from the player
                dirX = -dirX;
                dirY = -dirY;
                speed = 28.0f;
            } else if (currentDist < PREFERRED_DIST) {
                // Hold stance
                speed = 0.0f;
            }
        }

        phys.velX[i] = dirX * speed;
        phys.velY[i] = dirY * speed;
    }
}

void PhysicsSystem::ResolveHordeSeparation(
    ECS::PhysicsSoA& phys,
    uint32_t activeCount,
    const Spatial::SpatialHashGrid& grid,
    float dt
) {
    constexpr float SEPARATION_STRENGTH = 42.0f; // Directly replicates PokeSurvivor enemy.js constant
    constexpr float OVERLAP_THRESHOLD  = 0.82f; // Overlap factor from original game
    constexpr uint32_t MAX_CONTACTS    = 6;     // Hard limit per entity to avoid excessive force jitter

    for (uint32_t i = 0; i < activeCount; ++i) {
        if (!(phys.flags[i] & ECS::FLAG_ACTIVE)) continue;

        const float ex = phys.posX[i];
        const float ey = phys.posY[i];
        const float er = phys.radius[i];

        int32_t cx = static_cast<int32_t>(ex * Spatial::INV_CELL_SIZE) + Spatial::HALF_WIDTH;
        int32_t cy = static_cast<int32_t>(ey * Spatial::INV_CELL_SIZE) + Spatial::HALF_HEIGHT;

        if (static_cast<uint32_t>(cx) >= Spatial::GRID_WIDTH || static_cast<uint32_t>(cy) >= Spatial::GRID_HEIGHT) {
            continue;
        }

        float repulseX = 0.0f;
        float repulseY = 0.0f;
        uint32_t contacts = 0;

        // Traverse 3x3 neighborhood around the entity
        for (int32_t dy = -1; dy <= 1; ++dy) {
            int32_t ncy = cy + dy;
            if (static_cast<uint32_t>(ncy) >= Spatial::GRID_HEIGHT) continue;
            int32_t rowOffset = ncy * Spatial::GRID_WIDTH;

            for (int32_t dx = -1; dx <= 1; ++dx) {
                int32_t ncx = cx + dx;
                if (static_cast<uint32_t>(ncx) >= Spatial::GRID_WIDTH) continue;

                const Spatial::GridCell& cell = grid.cells[ncx + rowOffset];
                const uint16_t cellCount = cell.count;

                for (uint16_t c = 0; c < cellCount; ++c) {
                    uint32_t otherIdx = cell.entities[c];
                    if (otherIdx == i) continue;

                    float ox = phys.posX[otherIdx];
                    float oy = phys.posY[otherIdx];
                    float orad = phys.radius[otherIdx];

                    float minDist = (er + orad) * OVERLAP_THRESHOLD;
                    float diffX = ex - ox;
                    float diffY = ey - oy;
                    float distSq = diffX * diffX + diffY * diffY;

                    if (distSq < (minDist * minDist) && distSq > 1e-4f) {
                        float dist = sqrtf(distSq);
                        float factor = (minDist - dist) / minDist;
                        float invDist = 1.0f / dist;

                        repulseX += (diffX * invDist) * factor;
                        repulseY += (diffY * invDist) * factor;

                        if (++contacts >= MAX_CONTACTS) {
                            goto apply_repulsion;
                        }
                    }
                }
            }
        }

    apply_repulsion:
        if (contacts > 0) {
            phys.posX[i] += repulseX * SEPARATION_STRENGTH * dt;
            phys.posY[i] += repulseY * SEPARATION_STRENGTH * dt;
        }
    }
}

void PhysicsSystem::Integrate(
    ECS::PhysicsSoA& phys,
    uint32_t activeCount,
    float dt
) {
    for (uint32_t i = 0; i < activeCount; ++i) {
        if (phys.flags[i] & ECS::FLAG_ACTIVE) {
            phys.posX[i] += phys.velX[i] * dt;
            phys.posY[i] += phys.velY[i] * dt;
        }
    }
}

void PhysicsSystem::ResolvePlayerCollisions(
    const ECS::PhysicsSoA& phys,
    const ECS::CombatSoA& combat,
    ECS::PlayerData& player,
    const Spatial::SpatialHashGrid& grid,
    float dt
) {
    if (player.invulnerableTimer > 0.0f) {
        player.invulnerableTimer -= dt;
        return;
    }

    const float px = player.x;
    const float py = player.y;
    const float pr = player.radius;

    // Fast 3x3 local grid query around player
    grid.query3x3(px, py, [&](uint16_t enemyIdx) {
        if (!(phys.flags[enemyIdx] & ECS::FLAG_ACTIVE)) return;

        float diffX = px - phys.posX[enemyIdx];
        float diffY = py - phys.posY[enemyIdx];
        float distSq = diffX * diffX + diffY * diffY;
        float combinedR = pr + phys.radius[enemyIdx];

        if (distSq <= combinedR * combinedR) {
            // Apply contact damage
            int32_t dmg = combat.baseDamage[enemyIdx];
            player.hp -= (dmg > 0 ? dmg : 5);
            if (player.hp < 0) player.hp = 0;

            // 0.6 seconds invulnerability grace period
            player.invulnerableTimer = 0.6f;

            // Trigger passive audio SFX via NDSP
            Audio::AudioManager::Instance().PlaySFX(Audio::SoundID::HIT, 1.0f);
        }
    });
}

void PhysicsSystem::Step(
    ECS::PhysicsSoA& phys,
    ECS::CombatSoA& combat,
    const ECS::AISoA& ai,
    ECS::PlayerData& player,
    uint32_t activeCount,
    Spatial::SpatialHashGrid& grid,
    float dt
) {
    // 1. Steering & AI Velocity Calculation
    UpdateSteering(phys, ai, player, activeCount, dt);

    // 2. Linear Position Integration
    Integrate(phys, activeCount, dt);

    // 3. Grid Rebuild (O(N) contiguous linear write)
    RebuildSpatialGrid(grid, phys, activeCount);

    // 4. Horde Separation (3x3 spatial repulsion)
    ResolveHordeSeparation(phys, activeCount, grid, dt);

    // 5. Player Contact Collision
    ResolvePlayerCollisions(phys, combat, player, grid, dt);
}

} // namespace PokeSurvivor::Physics
