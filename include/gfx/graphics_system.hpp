#pragma once

#include <3ds.h>
#include <citro2d.h>
#include "ecs/entity_types.hpp"
#include "gfx/texture_manager.hpp"

namespace PokeSurvivor::Gfx {

constexpr u32 TOP_WIDTH     = 400;
constexpr u32 TOP_HEIGHT    = 240;
constexpr u32 BOTTOM_WIDTH  = 320;
constexpr u32 BOTTOM_HEIGHT = 240;

/**
 * Graphics System utilizing Citro2D and Citro3D.
 * Manages dual-screen rendering, frustum culling, sprite tinting, and bottom UI.
 */
class GraphicsSystem {
public:
    static void Init(C3D_RenderTarget* topTarget, C3D_RenderTarget* botTarget);
    static void Cleanup();

    static void BeginFrame();

    static void RenderTopScreen(
        const ECS::PhysicsSoA& phys,
        const ECS::RenderSoA& render,
        const ECS::PlayerData& player,
        float camX,
        float camY,
        uint32_t activeCount,
        TextureManager& texMgr
    );

    static void RenderBottomScreen(
        const ECS::PlayerData& player,
        uint32_t activeEnemies,
        float fps,
        bool isNew3DS,
        TextureManager& texMgr
    );

    static void EndFrame();
};

} // namespace PokeSurvivor::Gfx
