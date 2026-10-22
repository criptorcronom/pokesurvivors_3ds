#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace PokeSurvivor::Gfx {

// Maximum active .t3x spritesheets in VRAM simultaneously.
// At ~512 KB to 1 MB per 1024x1024 sheet, 8 sheets is the safe sweet spot (<= 4 MB VRAM).
constexpr size_t MAX_LOADED_SHEETS = 8;

// Predefined Sheet Identifiers
constexpr uint32_t SHEET_PLAYER     = 0x1000;
constexpr uint32_t SHEET_UI         = 0x1001;
constexpr uint32_t SHEET_VFX        = 0x1002;
constexpr uint32_t SHEET_BIOME_BASE = 0x2000; // Biome 0 = 0x2000, Biome 1 = 0x2001, etc.

/**
 * Tracks an active C2D_SpriteSheet allocation in PICA200 VRAM.
 */
struct SheetSlot {
    C2D_SpriteSheet handle = nullptr;
    uint32_t pageId        = 0;
    uint32_t biomeTag      = 0;
    uint64_t lastUsedTick  = 0;
    bool isResident        = false; // Resident sheets (Player, UI) are exempt from LRU purge
    bool inUse             = false;
};

/**
 * Texture & Asset Manager designed for Nintendo 3DS 6 MB VRAM constraints.
 * Implements Texture Paging, LRU Cache Eviction, and Active VRAM Purging.
 */
class TextureManager {
public:
    SheetSlot slots[MAX_LOADED_SHEETS];
    uint32_t activeSheetCount = 0;

    TextureManager() {
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            slots[i] = SheetSlot{};
        }
    }

    ~TextureManager() {
        PurgeAll();
    }

    /**
     * Loads a .t3x file from RomFS directly into VRAM using C2D_SpriteSheetLoad.
     * If the sheet table is full, evicts the least recently used non-resident sheet.
     */
    bool LoadSheet(uint32_t pageId, const char* romfsPath, uint32_t biomeTag = 0, bool isResident = false) {
        // Return true immediately if already loaded
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && slots[i].pageId == pageId) {
                slots[i].lastUsedTick = svcGetSystemTick();
                return true;
            }
        }

        // If at max capacity, evict least recently used non-resident sheet
        if (activeSheetCount >= MAX_LOADED_SHEETS) {
            if (!EvictLRU()) {
                // All slots are resident and cannot be freed
                return false;
            }
        }

        // Find free slot
        int freeSlot = -1;
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (!slots[i].inUse) {
                freeSlot = static_cast<int>(i);
                break;
            }
        }

        if (freeSlot < 0) return false;

        // Bare-metal C2D texture allocation in VRAM
        C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(romfsPath);
        if (!sheet) {
            // File not found or corrupt .t3x
            return false;
        }

        slots[freeSlot].handle       = sheet;
        slots[freeSlot].pageId       = pageId;
        slots[freeSlot].biomeTag     = biomeTag;
        slots[freeSlot].lastUsedTick = svcGetSystemTick();
        slots[freeSlot].isResident   = isResident;
        slots[freeSlot].inUse        = true;

        activeSheetCount++;
        return true;
    }

    /**
     * Imperatively purges a specific sheet from VRAM.
     */
    void FreeSheet(uint32_t pageId) {
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && slots[i].pageId == pageId) {
                if (slots[i].handle) {
                    C2D_SpriteSheetFree(slots[i].handle);
                    slots[i].handle = nullptr;
                }
                slots[i].inUse = false;
                slots[i].pageId = 0;
                activeSheetCount--;
                return;
            }
        }
    }

    /**
     * Purges all sheets associated with an expired biome or finished wave.
     * Prevents the 6 MB VRAM from accumulating orphaned horde textures.
     */
    void PurgeBiome(uint32_t biomeTag) {
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && !slots[i].isResident && slots[i].biomeTag == biomeTag) {
                if (slots[i].handle) {
                    C2D_SpriteSheetFree(slots[i].handle);
                    slots[i].handle = nullptr;
                }
                slots[i].inUse = false;
                slots[i].pageId = 0;
                activeSheetCount--;
            }
        }
    }

    /**
     * Evicts the oldest non-resident sheet to liberate VRAM.
     */
    bool EvictLRU() {
        int oldestSlot = -1;
        uint64_t oldestTick = 0xFFFFFFFFFFFFFFFFULL;

        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && !slots[i].isResident) {
                if (slots[i].lastUsedTick < oldestTick) {
                    oldestTick = slots[i].lastUsedTick;
                    oldestSlot = static_cast<int>(i);
                }
            }
        }

        if (oldestSlot >= 0) {
            C2D_SpriteSheetFree(slots[oldestSlot].handle);
            slots[oldestSlot].handle = nullptr;
            slots[oldestSlot].inUse  = false;
            slots[oldestSlot].pageId = 0;
            activeSheetCount--;
            return true;
        }

        return false;
    }

    /**
     * Purges all active sheets upon application exit or scene restart.
     */
    void PurgeAll() {
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && slots[i].handle) {
                C2D_SpriteSheetFree(slots[i].handle);
                slots[i].handle = nullptr;
                slots[i].inUse  = false;
            }
        }
        activeSheetCount = 0;
    }

    /**
     * Retrieves a subtexture C2D_Image from a loaded sheet.
     */
    bool GetImage(uint32_t pageId, size_t index, C2D_Image& outImage) {
        for (size_t i = 0; i < MAX_LOADED_SHEETS; ++i) {
            if (slots[i].inUse && slots[i].pageId == pageId) {
                slots[i].lastUsedTick = svcGetSystemTick();
                size_t count = C2D_SpriteSheetCount(slots[i].handle);
                if (index < count) {
                    outImage = C2D_SpriteSheetGetImage(slots[i].handle, index);
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    /**
     * Maps continuous animation time (dt) to frame indices.
     * AnimStates:
     *   0: Idle   (Loops: 4 frames @ 4 fps)
     *   1: Walk   (Loops: 4 frames @ 8 fps)
     *   2: Attack (One-shot / clamp)
     *   3: Hurt   (Fixed short flash)
     *   4: Faint  (Clamps on last ground frame)
     */
    static inline size_t CalculateAnimationFrame(
        uint8_t animState,
        float animTimer,
        size_t framesPerState = 4
    ) {
        if (framesPerState == 0) return 0;

        float fps = 8.0f;
        if (animState == 0) fps = 4.0f; // Idle is slower
        else if (animState == 2) fps = 12.0f; // Attack is faster

        uint32_t frame = static_cast<uint32_t>(animTimer * fps);

        if (animState == 4) {
            // Faint: Clamps at final death frame
            return (frame >= framesPerState) ? (framesPerState - 1) : frame;
        }

        return frame % framesPerState;
    }
};

} // namespace PokeSurvivor::Gfx
