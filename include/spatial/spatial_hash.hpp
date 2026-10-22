#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include "ecs/entity_types.hpp"

namespace PokeSurvivor::Spatial {

// Cell dimensions tailored to Pokemon body sizes and typical spell radiuses
constexpr float CELL_SIZE             = 52.0f; // Mirrors original JS CELL constant
constexpr float INV_CELL_SIZE         = 1.0f / CELL_SIZE;

// 2D Spatial Dimensions (Flat Array): 64 x 64 cells = 3328 x 3328 world pixels.
// Comfortably encompasses the 3DS viewport (400x240) and max enemy ring boundaries.
constexpr int32_t GRID_WIDTH          = 64;
constexpr int32_t GRID_HEIGHT         = 64;
constexpr uint32_t TOTAL_CELLS        = GRID_WIDTH * GRID_HEIGHT;

// Center offset so world coordinates around (0,0) map cleanly into positive grid space
constexpr int32_t HALF_WIDTH          = GRID_WIDTH >> 1;
constexpr int32_t HALF_HEIGHT         = GRID_HEIGHT >> 1;

// Maximum entity index capacity per cell.
// With up to 512 enemies dispersed over 4096 cells, 16 is more than sufficient.
constexpr uint16_t MAX_ENTITIES_PER_CELL = 16;

/**
 * Lightweight, fixed-size cell structure with static memory layout.
 * Total size: 2 bytes (count) + 32 bytes (16 x uint16_t) = 34 bytes per cell.
 * Total grid footprint: ~139 KB in RAM (negligible on 3DS 64MB memory).
 */
struct alignas(2) GridCell {
    uint16_t count;
    uint16_t entities[MAX_ENTITIES_PER_CELL];
};

/**
 * Bare-metal Spatial Hash Grid for Nintendo 3DS (ARM1176JZF-S).
 * Guarantees zero dynamic memory allocations, zero pointer indirection,
 * and high cache locality.
 */
class SpatialHashGrid {
public:
    GridCell cells[TOTAL_CELLS];

    SpatialHashGrid() {
        clear();
    }

    /**
     * Fast clear: resets the count of all cells.
     * ARM11 can write 4096 uint16_t values (~8 KB) in a few microseconds.
     */
    inline void clear() {
        for (uint32_t i = 0; i < TOTAL_CELLS; ++i) {
            cells[i].count = 0;
        }
    }

    /**
     * Converts world-space (x, y) into a 1D flat cell index.
     * Uses unsigned conversion for a single-branch bounds check.
     */
    inline int32_t getCellIndex(float x, float y) const {
        int32_t cx = static_cast<int32_t>(x * INV_CELL_SIZE) + HALF_WIDTH;
        int32_t cy = static_cast<int32_t>(y * INV_CELL_SIZE) + HALF_HEIGHT;

        if (static_cast<uint32_t>(cx) >= GRID_WIDTH || static_cast<uint32_t>(cy) >= GRID_HEIGHT) {
            return -1; // Out of tracked world bounds
        }
        return cx + cy * GRID_WIDTH;
    }

    /**
     * Inserts an active entity into the grid.
     * Safe insertion: if the cell limit is exceeded, the excess is dropped to prevent memory corruption.
     */
    inline void insert(float x, float y, uint16_t entityIdx) {
        int32_t idx = getCellIndex(x, y);
        if (idx >= 0) {
            GridCell& cell = cells[idx];
            if (cell.count < MAX_ENTITIES_PER_CELL) {
                cell.entities[cell.count++] = entityIdx;
            }
        }
    }

    /**
     * Iterates over all entities in the 3x3 local grid surrounding (x, y).
     * Accepts a callable lambda: void(uint16_t entityIdx).
     */
    template <typename Callback>
    inline void query3x3(float x, float y, Callback&& cb) const {
        int32_t cx = static_cast<int32_t>(x * INV_CELL_SIZE) + HALF_WIDTH;
        int32_t cy = static_cast<int32_t>(y * INV_CELL_SIZE) + HALF_HEIGHT;

        for (int32_t dy = -1; dy <= 1; ++dy) {
            int32_t ncy = cy + dy;
            if (static_cast<uint32_t>(ncy) >= GRID_HEIGHT) continue;
            int32_t rowOffset = ncy * GRID_WIDTH;

            for (int32_t dx = -1; dx <= 1; ++dx) {
                int32_t ncx = cx + dx;
                if (static_cast<uint32_t>(ncx) >= GRID_WIDTH) continue;

                const GridCell& cell = cells[ncx + rowOffset];
                const uint16_t cellCount = cell.count;

                for (uint16_t c = 0; c < cellCount; ++c) {
                    cb(cell.entities[c]);
                }
            }
        }
    }

    /**
     * Range query for area-of-effect spells, projectiles, or player pickup magnet.
     */
    template <typename Callback>
    inline void queryCircle(float x, float y, float radius, const ECS::PhysicsSoA& phys, Callback&& cb) const {
        int32_t minX = static_cast<int32_t>((x - radius) * INV_CELL_SIZE) + HALF_WIDTH;
        int32_t maxX = static_cast<int32_t>((x + radius) * INV_CELL_SIZE) + HALF_WIDTH;
        int32_t minY = static_cast<int32_t>((y - radius) * INV_CELL_SIZE) + HALF_HEIGHT;
        int32_t maxY = static_cast<int32_t>((y + radius) * INV_CELL_SIZE) + HALF_HEIGHT;

        // Clamp to grid dimensions
        if (minX < 0) minX = 0;
        if (maxX >= GRID_WIDTH) maxX = GRID_WIDTH - 1;
        if (minY < 0) minY = 0;
        if (maxY >= GRID_HEIGHT) maxY = GRID_HEIGHT - 1;

        const float radiusSq = radius * radius;

        for (int32_t cy = minY; cy <= maxY; ++cy) {
            int32_t rowOffset = cy * GRID_WIDTH;
            for (int32_t cx = minX; cx <= maxX; ++cx) {
                const GridCell& cell = cells[cx + rowOffset];
                const uint16_t cellCount = cell.count;

                for (uint16_t c = 0; c < cellCount; ++c) {
                    uint16_t idx = cell.entities[c];
                    float diffX = phys.posX[idx] - x;
                    float diffY = phys.posY[idx] - y;
                    float combinedR = radius + phys.radius[idx];

                    if ((diffX * diffX + diffY * diffY) <= (combinedR * combinedR)) {
                        cb(idx);
                    }
                }
            }
        }
    }
};

} // namespace PokeSurvivor::Spatial
