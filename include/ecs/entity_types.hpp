#pragma once

#include <cstdint>
#include <cstddef>

namespace PokeSurvivor::ECS {

// Maximum active entities tuned for Old 3DS memory constraints (ARM11 @ 268 MHz, 32KB L1 cache)
constexpr size_t MAX_ENEMIES     = 512; // Covers the 420 horde peak with power-of-2 cache alignment
constexpr size_t MAX_PROJECTILES = 128;
constexpr size_t MAX_PICKUPS     = 256;
constexpr size_t MAX_VFX         = 64;

// Entity status and attribute bitflags
enum EnemyFlags : uint16_t {
    FLAG_ACTIVE      = 1 << 0,
    FLAG_FLYING      = 1 << 1,
    FLAG_BOSS        = 1 << 2,
    FLAG_SHINY       = 1 << 3,
    FLAG_STUNNED     = 1 << 4,
    FLAG_FACING_LEFT = 1 << 5,
    FLAG_HURT_FLASH  = 1 << 6,
    FLAG_IN_CAMERA   = 1 << 7
};

// Enemy AI Behaviors mirroring original JS definitions
enum class BehaviorType : uint8_t {
    CHASER,     // Standard direct chase
    CHARGER,    // Telegraphs then dashes
    RANGED,     // Stops and fires projectiles
    SHIELDER,   // Provides shields to allies
    HEALER,     // Restores HP to nearby allies
    SUMMONER,   // Spawns minion waves
    BURROWER    // Dives underground/immune phases
};

// Elemental Types from Pokemon type table
enum class ElementType : uint8_t {
    NORMAL, FIRE, WATER, GRASS, ELECTRIC, ICE,
    FIGHTING, POISON, GROUND, FLYING, PSYCHIC, BUG,
    ROCK, GHOST, DRAGON, STEEL, DARK, FAIRY, COUNT
};

// =============================================================================
// Cache-Tiered Structure of Arrays (SoA) for Enemies
// =============================================================================

// Tier 1 (Hot): Transform & Physics Data.
// Updated every frame for all active entities.
// 24 bytes per entity: 512 entities = 12 KB, completely residency in ARM11 32KB L1 Cache!
struct alignas(32) PhysicsSoA {
    float posX[MAX_ENEMIES];
    float posY[MAX_ENEMIES];
    float velX[MAX_ENEMIES];
    float velY[MAX_ENEMIES];
    float radius[MAX_ENEMIES];
    uint16_t flags[MAX_ENEMIES];
    uint16_t pad[MAX_ENEMIES];
};

// Tier 2 (Warm): Health, Combat & Status Effects.
// Accessed only during damage calculation, spatial overlap, and status ticks.
struct alignas(32) CombatSoA {
    int32_t hp[MAX_ENEMIES];
    int32_t maxHp[MAX_ENEMIES];
    int16_t baseDamage[MAX_ENEMIES];
    int16_t shieldHp[MAX_ENEMIES];
    ElementType primaryType[MAX_ENEMIES];
    uint8_t statusEffect[MAX_ENEMIES]; // Bitmask: bit 0: Burn, bit 1: Poison, bit 2: Slow, bit 3: Freeze
    float statusDuration[MAX_ENEMIES];
    float statusTickTimer[MAX_ENEMIES];
    float knockbackX[MAX_ENEMIES];
    float knockbackY[MAX_ENEMIES];
    uint16_t xpReward[MAX_ENEMIES];
};

// Tier 3 (Render Hot): Visual & Animation Data.
// Streamed sequentially during Citro2D draw pass on the Top Screen.
struct alignas(32) RenderSoA {
    uint16_t dexId[MAX_ENEMIES];
    uint8_t animState[MAX_ENEMIES]; // 0: Idle, 1: Walk, 2: Attack, 3: Hurt, 4: Faint
    uint8_t animFrame[MAX_ENEMIES];
    float animTimer[MAX_ENEMIES];
    float scale[MAX_ENEMIES];
    uint8_t flashCounter[MAX_ENEMIES];
};

// Tier 4 (Cold): AI State, Timers, and Cooldowns.
// Updated either round-robin or on fractional ticks.
struct alignas(32) AISoA {
    BehaviorType behavior[MAX_ENEMIES];
    float actionTimer[MAX_ENEMIES];
    float cooldown[MAX_ENEMIES];
    float targetAngle[MAX_ENEMIES];
    uint8_t minionCount[MAX_ENEMIES];
};

// Player Character State (Single player / Host)
struct PlayerData {
    float x, y;
    float vx, vy;
    float speed;
    float radius;
    int32_t hp, maxHp;
    uint32_t level;
    uint32_t xp, xpNext;
    uint32_t coins;
    uint16_t dexId;
    bool isShiny;
    bool facingLeft;
    float invulnerableTimer;
};

} // namespace PokeSurvivor::ECS
