/**
 * PokeSurvivors 3DS - Native C++ Port
 * Target: Nintendo 3DS (Old 3DS & New 3DS)
 * Toolchain: devkitARM / libctru / Citro2D / Citro3D / NDSP
 */

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "ecs/entity_manager.hpp"
#include "physics/physics_system.hpp"
#include "spatial/spatial_hash.hpp"
#include "gfx/texture_manager.hpp"
#include "gfx/graphics_system.hpp"
#include "audio/audio_manager.hpp"

namespace PokeSurvivor {

// Screen Dimensions
constexpr u32 TOP_WIDTH     = 400;
constexpr u32 TOP_HEIGHT    = 240;
constexpr u32 BOTTOM_WIDTH  = 320;
constexpr u32 BOTTOM_HEIGHT = 240;

// Game State Machine
enum class GameState {
    TITLE,
    PLAYING,
    LEVELUP_MENU,
    PAUSED,
    GAME_OVER
};

struct EngineContext {
    C3D_RenderTarget* topTarget = nullptr;
    C3D_RenderTarget* botTarget = nullptr;
    C2D_TextBuf staticTextBuf   = nullptr;
    C2D_TextBuf dynamicTextBuf  = nullptr;
    
    GameState state = GameState::PLAYING;
    ECS::EntityManager ecs;
    ECS::PlayerData player;
    Spatial::SpatialHashGrid spatialGrid;
    Gfx::TextureManager texManager;
    
    // Camera State
    float camX = 0.0f;
    float camY = 0.0f;
    
    // Performance & Timing
    u64 lastTick = 0;
    float dt = 0.0f;
    float fps = 60.0f;
    bool isNew3DS = false;
};

static EngineContext g_ctx;

// =============================================================================
// Hardware & Subsystem Initialization
// =============================================================================
bool InitSubsystems() {
    // 1. New 3DS Clock Boost & Cache Activation
    // Old 3DS safely ignores this call (stays at 268 MHz).
    // New 3DS jumps from 268 MHz to 804 MHz with L2 cache enabled.
    osSetSpeedupEnable(true);

    // Detect if running on New 3DS hardware
    u8 model = 0;
    CFGU_GetSystemModel(&model);
    g_ctx.isNew3DS = (model == 2 || model == 4 || model == 5); // N3DS, N3DS XL, N2DS XL

    // 2. Horizon OS Application & Graphics Init
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // 3. Dual-Screen Render Targets
    // Top Screen: 400x240 (citro3d / citro2d left eye target)
    // Bottom Screen: 320x240 (touch display)
    g_ctx.topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_ctx.botTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    if (!g_ctx.topTarget || !g_ctx.botTarget) {
        return false;
    }

    // 4. Text Buffers for HUD and Menus
    g_ctx.staticTextBuf  = C2D_TextBufNew(1024);
    g_ctx.dynamicTextBuf = C2D_TextBufNew(512);

    // 5. Audio Subsystem (NDSP Passive Playback & DMA Linear Queuing)
    // Avoids software synthesis and redirects pre-baked PCM buffers directly to DSP hardware
    Audio::AudioManager::Instance().Init();

    // 6. Thread priorities and Core limits
    // Main thread default priority is 0x30.
    // Allow the application thread to utilize up to 80% CPU on Core 1 if available
    APT_SetAppCpuTimeLimit(80);

    // 7. RomFS Filesystem & Graphics System
    romfsInit();
    Gfx::GraphicsSystem::Init(g_ctx.topTarget, g_ctx.botTarget);

    // Preload Resident Assets (Player & Initial Biome 0)
    g_ctx.texManager.LoadSheet(Gfx::SHEET_PLAYER, "romfs:/gfx/sheet_player.t3x", 0, true);
    g_ctx.texManager.LoadSheet(Gfx::SHEET_BIOME_BASE, "romfs:/gfx/sheet_biome_0.t3x", 0, false);

    // Preload SFX into DSP-accessible linear memory
    Audio::AudioManager::Instance().LoadSFX(Audio::SoundID::HIT, "romfs:/audio/hit.wav");
    Audio::AudioManager::Instance().LoadSFX(Audio::SoundID::COIN, "romfs:/audio/coin.wav");
    Audio::AudioManager::Instance().LoadSFX(Audio::SoundID::XP, "romfs:/audio/xp.wav");
    Audio::AudioManager::Instance().LoadSFX(Audio::SoundID::LEVELUP, "romfs:/audio/levelup.wav");

    // Start Biome 0 Background Music Streaming via Channel 0 Quad-Buffers
    Audio::AudioManager::Instance().PlayBGM("romfs:/audio/biome_0.wav", true);

    // Initialize player state
    g_ctx.player.x = 0.0f;
    g_ctx.player.y = 0.0f;
    g_ctx.player.vx = 0.0f;
    g_ctx.player.vy = 0.0f;
    g_ctx.player.speed = 95.0f; // Pixels per second
    g_ctx.player.radius = 10.0f;
    g_ctx.player.hp = 100;
    g_ctx.player.maxHp = 100;
    g_ctx.player.level = 1;
    g_ctx.player.xp = 0;
    g_ctx.player.xpNext = 20;
    g_ctx.player.dexId = 25; // Pikachu / starter
    g_ctx.player.isShiny = false;
    g_ctx.player.facingLeft = false;

    g_ctx.lastTick = svcGetSystemTick();
    return true;
}

// =============================================================================
// Resource Cleanup
// =============================================================================
void CleanupSubsystems() {
    Audio::AudioManager::Instance().Shutdown();
    g_ctx.texManager.PurgeAll();
    Gfx::GraphicsSystem::Cleanup();

    if (g_ctx.staticTextBuf)  C2D_TextBufDelete(g_ctx.staticTextBuf);
    if (g_ctx.dynamicTextBuf) C2D_TextBufDelete(g_ctx.dynamicTextBuf);

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
}

// =============================================================================
// Input Handling & Controls (Circle Pad, C-Stick, ZL/ZR, Touch)
// =============================================================================
void ProcessInputs() {
    hidScanInput();

    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    // Standard Exit
    if (kDown & KEY_START) {
        g_ctx.state = (g_ctx.state == GameState::PAUSED) ? GameState::PLAYING : GameState::PAUSED;
    }

    // Circle Pad - Analog Player Movement
    circlePosition cpad;
    hidCircleRead(&cpad);

    float normX = 0.0f;
    float normY = 0.0f;

    // Apply deadzone for Old 3DS drift prevention
    constexpr s16 DEADZONE = 30;
    if (std::abs(cpad.dx) > DEADZONE || std::abs(cpad.dy) > DEADZONE) {
        normX = static_cast<float>(cpad.dx) / 154.0f;
        normY = static_cast<float>(cpad.dy) / 154.0f;
        
        // Clamp length to 1.0f
        float len = sqrtf(normX * normX + normY * normY);
        if (len > 1.0f) {
            normX /= len;
            normY /= len;
        }
    }

    // DPAD fallback movement
    if (kHeld & KEY_DUP)    normY += 1.0f;
    if (kHeld & KEY_DDOWN)  normY -= 1.0f;
    if (kHeld & KEY_DLEFT)  normX -= 1.0f;
    if (kHeld & KEY_DRIGHT) normX += 1.0f;

    // Y axis in 3DS screen space is inverted relative to standard cartesian
    g_ctx.player.vx = normX * g_ctx.player.speed;
    g_ctx.player.vy = -normY * g_ctx.player.speed;

    if (normX < -0.1f) g_ctx.player.facingLeft = true;
    else if (normX > 0.1f) g_ctx.player.facingLeft = false;

    // New 3DS C-Stick - Manual Camera Pan / Aiming
    circlePosition cstick;
    hidCstickRead(&cstick);

    if (g_ctx.isNew3DS) {
        if (std::abs(cstick.dx) > DEADZONE || std::abs(cstick.dy) > DEADZONE) {
            g_ctx.camX += (static_cast<float>(cstick.dx) / 154.0f) * 120.0f * g_ctx.dt;
            g_ctx.camY -= (static_cast<float>(cstick.dy) / 154.0f) * 120.0f * g_ctx.dt;
        }
    }

    // New 3DS Extra Shoulder Buttons (ZL / ZR)
    if (kDown & KEY_ZL) {
        // Quick Page Prev in Menus / Dex
    }
    if (kDown & KEY_ZR) {
        // Quick Page Next in Menus / Dex
    }

    // Touch Screen Input (Bottom Screen Menu interaction)
    touchPosition touch;
    hidTouchRead(&touch);

    if (kDown & KEY_TOUCH) {
        // Handle bottom screen touch button clicks
    }
}

// =============================================================================
// Simulation Update Pass (Physics & ECS System Traversal)
// =============================================================================
void UpdateSimulation(float dt) {
    if (g_ctx.state != GameState::PLAYING) return;

    // 1. Update Player Position
    g_ctx.player.x += g_ctx.player.vx * dt;
    g_ctx.player.y += g_ctx.player.vy * dt;

    // Camera smoothly follows player (0.1f lerp)
    g_ctx.camX += (g_ctx.player.x - g_ctx.camX) * 0.1f;
    g_ctx.camY += (g_ctx.player.y - g_ctx.camY) * 0.1f;

    // 2. High-performance Bare-Metal Physics Step
    // Executes: Steering -> Linear Integration -> Spatial Hash Rebuild -> Horde Separation -> Player Collisions
    Physics::PhysicsSystem::Step(
        g_ctx.ecs.physics,
        g_ctx.ecs.combat,
        g_ctx.ecs.ai,
        g_ctx.player,
        g_ctx.ecs.activeEnemies,
        g_ctx.spatialGrid,
        dt
    );

    // 3. Animation State & Frame Advancement
    ECS::RenderSoA& rnd = g_ctx.ecs.render;
    const uint32_t activeEnemies = g_ctx.ecs.activeEnemies;
    for (uint32_t i = 0; i < activeEnemies; ++i) {
        if (g_ctx.ecs.physics.flags[i] & ECS::FLAG_ACTIVE) {
            rnd.animTimer[i] += dt;
            rnd.animFrame[i] = static_cast<uint8_t>(Gfx::TextureManager::CalculateAnimationFrame(
                rnd.animState[i], rnd.animTimer[i], 4
            ));
            if (rnd.flashCounter[i] > 0) rnd.flashCounter[i]--;
        }
    }
}

// =============================================================================
// Citro2D Dual-Screen Render Pass
// =============================================================================
void RenderFrame() {
    Gfx::GraphicsSystem::BeginFrame();

    // Top Screen: World Viewport, Camera, Enemies & Player
    Gfx::GraphicsSystem::RenderTopScreen(
        g_ctx.ecs.physics,
        g_ctx.ecs.render,
        g_ctx.player,
        g_ctx.camX,
        g_ctx.camY,
        g_ctx.ecs.activeEnemies,
        g_ctx.texManager
    );

    // Bottom Screen: High-Tech Dark Mode HUD, Health/XP Bars & Touch Slots
    Gfx::GraphicsSystem::RenderBottomScreen(
        g_ctx.player,
        g_ctx.ecs.activeEnemies,
        g_ctx.fps,
        g_ctx.isNew3DS,
        g_ctx.texManager
    );

    Gfx::GraphicsSystem::EndFrame();
}

} // namespace PokeSurvivor

// =============================================================================
// Application Entry Point
// =============================================================================
int main(int argc, char* argv[]) {
    if (!PokeSurvivor::InitSubsystems()) {
        PokeSurvivor::CleanupSubsystems();
        return -1;
    }

    // Spawn initial horde test wave (420 enemies matching the peak horde size)
    for (int i = 0; i < 420; ++i) {
        float angle = (static_cast<float>(i) / 420.0f) * 6.2831853f;
        float dist = 220.0f + static_cast<float>(i % 12) * 24.0f;
        PokeSurvivor::g_ctx.ecs.spawnEnemy(
            cosf(angle) * dist,
            sinf(angle) * dist,
            19, // Rattata
            25,
            5,
            35.0f,
            (i % 5 == 0) ? PokeSurvivor::ECS::BehaviorType::CHARGER : PokeSurvivor::ECS::BehaviorType::CHASER,
            8.0f
        );
    }

    // Main Horizon OS Loop
    while (aptMainLoop()) {
        // High resolution delta time
        u64 currentTick = svcGetSystemTick();
        u64 elapsedTicks = currentTick - PokeSurvivor::g_ctx.lastTick;
        PokeSurvivor::g_ctx.lastTick = currentTick;

        // 3DS CPU Base frequency: 268,111,856 Hz
        PokeSurvivor::g_ctx.dt = static_cast<float>(elapsedTicks) / 268111856.0f;
        if (PokeSurvivor::g_ctx.dt > 0.05f) PokeSurvivor::g_ctx.dt = 0.05f; // Clamp to avoid physics explosion

        // 1. Inputs
        PokeSurvivor::ProcessInputs();

        // 2. Logic & Physics
        PokeSurvivor::UpdateSimulation(PokeSurvivor::g_ctx.dt);

        // 3. Citro2D Render pass
        PokeSurvivor::RenderFrame();
    }

    PokeSurvivor::CleanupSubsystems();
    return 0;
}
