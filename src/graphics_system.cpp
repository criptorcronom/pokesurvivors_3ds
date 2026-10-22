#include "gfx/graphics_system.hpp"
#include <cstdio>
#include <cmath>

namespace PokeSurvivor::Gfx {

static C3D_RenderTarget* s_topTarget = nullptr;
static C3D_RenderTarget* s_botTarget = nullptr;
static C2D_TextBuf       s_dynamicBuf = nullptr;

void GraphicsSystem::Init(C3D_RenderTarget* topTarget, C3D_RenderTarget* botTarget) {
    s_topTarget = topTarget;
    s_botTarget = botTarget;
    s_dynamicBuf = C2D_TextBufNew(512);
}

void GraphicsSystem::Cleanup() {
    if (s_dynamicBuf) {
        C2D_TextBufDelete(s_dynamicBuf);
        s_dynamicBuf = nullptr;
    }
}

void GraphicsSystem::BeginFrame() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}

void GraphicsSystem::RenderTopScreen(
    const ECS::PhysicsSoA& phys,
    const ECS::RenderSoA& render,
    const ECS::PlayerData& player,
    float camX,
    float camY,
    uint32_t activeCount,
    TextureManager& texMgr
) {
    C2D_TargetClear(s_topTarget, C2D_Color32(20, 24, 38, 255));
    C2D_SceneBegin(s_topTarget);

    const float screenCenterX = TOP_WIDTH * 0.5f;
    const float screenCenterY = TOP_HEIGHT * 0.5f;

    // 1. Scrolling Arena Floor Background
    // Smooth infinite tiling offset by camera coordinates
    constexpr float TILE_SIZE = 40.0f;
    float startX = -fmodf(camX, TILE_SIZE);
    float startY = -fmodf(camY, TILE_SIZE);
    if (startX > 0.0f) startX -= TILE_SIZE;
    if (startY > 0.0f) startY -= TILE_SIZE;

    u32 gridLineColor = C2D_Color32(30, 36, 56, 255);
    for (float x = startX; x <= TOP_WIDTH + TILE_SIZE; x += TILE_SIZE) {
        C2D_DrawRectSolid(x, 0.0f, 0.0f, 1.0f, TOP_HEIGHT, gridLineColor);
    }
    for (float y = startY; y <= TOP_HEIGHT + TILE_SIZE; y += TILE_SIZE) {
        C2D_DrawRectSolid(0.0f, y, 0.0f, TOP_WIDTH, 1.0f, gridLineColor);
    }

    // 2. Render Active Enemies with Frustum Culling
    u32 shadowColor    = C2D_Color32(10, 12, 20, 110);
    u32 fallbackColor  = C2D_Color32(230, 55, 65, 255);
    u32 outlineColor   = C2D_Color32(120, 20, 30, 255);
    u32 chargerColor   = C2D_Color32(245, 140, 30, 255);

    C2D_ImageTint hurtTint;
    C2D_PlainImageTint(&hurtTint, C2D_Color32(255, 255, 255, 255), 0.85f);

    for (uint32_t i = 0; i < activeCount; ++i) {
        if (!(phys.flags[i] & ECS::FLAG_ACTIVE)) continue;

        float scrX = phys.posX[i] - camX + screenCenterX;
        float scrY = phys.posY[i] - camY + screenCenterY;
        float rad  = phys.radius[i];

        // Frustum Culling: discard off-screen sprites before any drawing
        if (scrX < -32.0f || scrX > TOP_WIDTH + 32.0f ||
            scrY < -32.0f || scrY > TOP_HEIGHT + 32.0f) {
            continue;
        }

        // Shadow under feet
        C2D_DrawEllipseSolid(scrX, scrY + rad * 0.7f, 0.0f, rad * 0.9f, rad * 0.35f, shadowColor);

        // Attempt sprite texture lookup from active .t3x sheet
        uint32_t pageId = SHEET_BIOME_BASE; // Default biome sheet
        C2D_Image spriteImage;
        bool hasTexture = texMgr.GetImage(pageId, render.animFrame[i], spriteImage);

        if (hasTexture) {
            float scaleX = (phys.flags[i] & ECS::FLAG_FACING_LEFT) ? -render.scale[i] : render.scale[i];
            float scaleY = render.scale[i];

            const C2D_ImageTint* tint = (render.flashCounter[i] > 0) ? &hurtTint : nullptr;
            C2D_DrawImageAt(spriteImage, scrX, scrY, 0.5f, tint, scaleX, scaleY);
        } else {
            // Crisp, high-contrast procedural fallback sprite
            u32 bodyCol = (phys.flags[i] & ECS::FLAG_BOSS) ? C2D_Color32(180, 50, 220, 255)
                        : (rad > 10.0f) ? chargerColor : fallbackColor;

            if (render.flashCounter[i] > 0) bodyCol = C2D_Color32(255, 255, 255, 255);

            C2D_DrawCircleSolid(scrX, scrY, 0.5f, rad, outlineColor);
            C2D_DrawCircleSolid(scrX, scrY, 0.5f, rad - 1.5f, bodyCol);

            // Facing direction pupil / indicator
            float eyeOffsetX = (phys.flags[i] & ECS::FLAG_FACING_LEFT) ? -rad * 0.4f : rad * 0.4f;
            C2D_DrawCircleSolid(scrX + eyeOffsetX, scrY - rad * 0.2f, 0.6f, 1.8f, C2D_Color32(255, 255, 255, 255));
            C2D_DrawCircleSolid(scrX + eyeOffsetX, scrY - rad * 0.2f, 0.7f, 1.0f, C2D_Color32(20, 20, 20, 255));
        }
    }

    // 3. Render Player Character
    float pScrX = player.x - camX + screenCenterX;
    float pScrY = player.y - camY + screenCenterY;
    float pRad  = player.radius;

    // Player Shadow
    C2D_DrawEllipseSolid(pScrX, pScrY + pRad * 0.75f, 0.0f, pRad * 1.1f, pRad * 0.4f, shadowColor);

    // Invulnerability Blink (skips frame rendering on alternating ticks)
    bool isBlinking = (player.invulnerableTimer > 0.0f) && (fmodf(player.invulnerableTimer * 20.0f, 2.0f) > 1.0f);

    if (!isBlinking) {
        C2D_Image playerSprite;
        bool hasPlayerTex = texMgr.GetImage(SHEET_PLAYER, 0, playerSprite);

        if (hasPlayerTex) {
            float pScaleX = player.facingLeft ? -1.0f : 1.0f;
            C2D_DrawImageAt(playerSprite, pScrX, pScrY, 0.5f, nullptr, pScaleX, 1.0f);
        } else {
            // Iconic Pikachu Yellow (#ffdf12) with electric cheeks
            C2D_DrawCircleSolid(pScrX, pScrY, 0.5f, pRad + 1.5f, C2D_Color32(160, 110, 0, 255));
            C2D_DrawCircleSolid(pScrX, pScrY, 0.6f, pRad, C2D_Color32(255, 225, 30, 255));

            // Cheeks
            float cheekX = player.facingLeft ? -pRad * 0.5f : pRad * 0.5f;
            C2D_DrawCircleSolid(pScrX + cheekX, pScrY + pRad * 0.1f, 0.7f, 2.2f, C2D_Color32(230, 40, 40, 255));
        }
    }
}

void GraphicsSystem::RenderBottomScreen(
    const ECS::PlayerData& player,
    uint32_t activeEnemies,
    float fps,
    bool isNew3DS,
    TextureManager& texMgr
) {
    (void)texMgr;
    C2D_TargetClear(s_botTarget, C2D_Color32(14, 16, 26, 255));
    C2D_SceneBegin(s_botTarget);

    // Frame styling
    u32 panelBg     = C2D_Color32(22, 26, 42, 255);
    u32 panelBorder = C2D_Color32(40, 48, 76, 255);
    u32 barBg       = C2D_Color32(18, 20, 32, 255);
    u32 hpGreen     = C2D_Color32(40, 210, 80, 255);
    u32 xpCyan      = C2D_Color32(40, 160, 255, 255);
    u32 slotBg      = C2D_Color32(28, 34, 54, 255);
    u32 activeSlot  = C2D_Color32(255, 200, 40, 255);

    // 1. Top Status Banner
    C2D_DrawRectSolid(8.0f, 8.0f, 0.0f, 304.0f, 76.0f, panelBorder);
    C2D_DrawRectSolid(10.0f, 10.0f, 0.0f, 300.0f, 72.0f, panelBg);

    // Health Bar
    C2D_DrawRectSolid(20.0f, 20.0f, 0.0f, 280.0f, 16.0f, barBg);
    float hpRatio = (player.maxHp > 0) ? static_cast<float>(player.hp) / player.maxHp : 0.0f;
    if (hpRatio < 0.0f) hpRatio = 0.0f;
    if (hpRatio > 1.0f) hpRatio = 1.0f;
    C2D_DrawRectSolid(20.0f, 20.0f, 0.0f, 280.0f * hpRatio, 16.0f, hpGreen);

    // XP Bar
    C2D_DrawRectSolid(20.0f, 44.0f, 0.0f, 280.0f, 12.0f, barBg);
    float xpRatio = (player.xpNext > 0) ? static_cast<float>(player.xp) / player.xpNext : 0.0f;
    if (xpRatio < 0.0f) xpRatio = 0.0f;
    if (xpRatio > 1.0f) xpRatio = 1.0f;
    C2D_DrawRectSolid(20.0f, 44.0f, 0.0f, 280.0f * xpRatio, 12.0f, xpCyan);

    // Level Badge & Hardware Indicator
    C2D_DrawRectSolid(20.0f, 62.0f, 0.0f, 60.0f, 14.0f, slotBg);
    C2D_DrawRectSolid(230.0f, 62.0f, 0.0f, 70.0f, 14.0f, isNew3DS ? C2D_Color32(40, 100, 200, 255) : slotBg);

    // 2. Weapon / Move Slots (4 Quick Slots)
    constexpr float SLOT_START_X = 20.0f;
    constexpr float SLOT_Y       = 96.0f;
    constexpr float SLOT_SIZE    = 42.0f;
    constexpr float SLOT_GAP     = 18.0f;

    for (int i = 0; i < 4; ++i) {
        float sx = SLOT_START_X + i * (SLOT_SIZE + SLOT_GAP);
        u32 borderCol = (i == 0) ? activeSlot : panelBorder;
        C2D_DrawRectSolid(sx - 1.0f, SLOT_Y - 1.0f, 0.0f, SLOT_SIZE + 2.0f, SLOT_SIZE + 2.0f, borderCol);
        C2D_DrawRectSolid(sx, SLOT_Y, 0.0f, SLOT_SIZE, SLOT_SIZE, slotBg);
    }

    // 3. Touch Menu Interactive Buttons
    constexpr float BTN_Y = 160.0f;
    constexpr float BTN_H = 34.0f;
    constexpr float BTN_W = 135.0f;

    // Upgrades Button
    C2D_DrawRectSolid(20.0f, BTN_Y, 0.0f, BTN_W, BTN_H, panelBorder);
    C2D_DrawRectSolid(21.0f, BTN_Y + 1.0f, 0.0f, BTN_W - 2.0f, BTN_H - 2.0f, panelBg);

    // Pokédex Button
    C2D_DrawRectSolid(165.0f, BTN_Y, 0.0f, BTN_W, BTN_H, panelBorder);
    C2D_DrawRectSolid(166.0f, BTN_Y + 1.0f, 0.0f, BTN_W - 2.0f, BTN_H - 2.0f, panelBg);

    // 4. Performance & Horde Telemetry Bar (Bottom Strip)
    C2D_DrawRectSolid(10.0f, 210.0f, 0.0f, 300.0f, 20.0f, panelBg);

    if (s_dynamicBuf) {
        C2D_TextBufClear(s_dynamicBuf);
        char statsStr[64];
        std::snprintf(statsStr, sizeof(statsStr), "HORDE: %lu | FPS: %.0f | %s",
                      activeEnemies, fps, isNew3DS ? "N3DS 804MHz" : "O3DS 268MHz");

        C2D_Text textObj;
        C2D_TextParse(&textObj, s_dynamicBuf, statsStr);
        C2D_TextOptimize(&textObj);
        C2D_DrawText(&textObj, C2D_WithColor, 18.0f, 213.0f, 0.5f, 0.45f, 0.45f, C2D_Color32(180, 200, 240, 255));
    }
}

void GraphicsSystem::EndFrame() {
    C3D_FrameEnd(0);
}

} // namespace PokeSurvivor::Gfx
