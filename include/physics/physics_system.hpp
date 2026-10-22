#pragma once

#include "ecs/entity_types.hpp"
#include "spatial/spatial_hash.hpp"

namespace PokeSurvivor::Physics {

/**
 * High-performance, bare-metal physics and collision system for Nintendo 3DS.
 * Operates purely on single-precision 32-bit floats (VFPv2 hardware acceleration).
 */
class PhysicsSystem {
public:
    /**
     * Clears and repopulates the static spatial hash grid with active entities.
     * Cost: O(N) linear sweep through Tier 1 (Hot Data) cache.
     */
    static void RebuildSpatialGrid(
        Spatial::SpatialHashGrid& grid,
        const ECS::PhysicsSoA& phys,
        uint32_t activeCount
    );

    /**
     * Steers enemies towards the nearest player or target coordinate.
     * Respects archetype behaviors (Chasers, Chargers, Ranged distance keepers).
     */
    static void UpdateSteering(
        ECS::PhysicsSoA& phys,
        const ECS::AISoA& ai,
        const ECS::PlayerData& player,
        uint32_t activeCount,
        float dt
    );

    /**
     * Replicates the horde repulsion physics from PokeSurvivor (enemy.js: separate).
     * Inspects 3x3 local grid cells and applies soft elastic repulsion to overlapping bodies.
     */
    static void ResolveHordeSeparation(
        ECS::PhysicsSoA& phys,
        uint32_t activeCount,
        const Spatial::SpatialHashGrid& grid,
        float dt
    );

    /**
     * Integrates positions from velocities and damps knockback forces.
     */
    static void Integrate(
        ECS::PhysicsSoA& phys,
        uint32_t activeCount,
        float dt
    );

    /**
     * Evaluates contact damage between active enemies and the player character.
     */
    static void ResolvePlayerCollisions(
        const ECS::PhysicsSoA& phys,
        const ECS::CombatSoA& combat,
        ECS::PlayerData& player,
        const Spatial::SpatialHashGrid& grid,
        float dt
    );

    /**
     * High-level coordinator that executes the full physics frame in optimal cache order.
     */
    static void Step(
        ECS::PhysicsSoA& phys,
        ECS::CombatSoA& combat,
        const ECS::AISoA& ai,
        ECS::PlayerData& player,
        uint32_t activeCount,
        Spatial::SpatialHashGrid& grid,
        float dt
    );
};

} // namespace PokeSurvivor::Physics
