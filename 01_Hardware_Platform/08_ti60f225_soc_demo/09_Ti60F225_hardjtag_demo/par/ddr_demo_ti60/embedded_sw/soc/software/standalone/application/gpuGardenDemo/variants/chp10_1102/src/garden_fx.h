/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only effects. The game state remains owned by garden_core.
 */
#ifndef GARDEN_FX_H
#define GARDEN_FX_H
#include "garden_core.h"
#include "garden_render.h"
#include "garden_heading_precision.h"

/* Critical visual semantics share core's exact arming rule across sources.
 * Forming sprites crop real art around its white-core pivot, never presenting
 * a full-size fully bright projectile while collision is disabled. */
static inline unsigned garden_fx_enemy_alpha(const GardenEnemyBullet *bullet)
{
    unsigned age=bullet ? bullet->age:0U;
    if(age>=GARDEN_BULLET_ARM_TICKS) age=GARDEN_BULLET_ARM_TICKS-1U;
    return garden_enemy_bullet_is_armed(bullet) ? 32U:6U+age*3U;
}
static inline unsigned garden_fx_enemy_asset(const GardenEnemyBullet *bullet,unsigned asset)
{
    return garden_enemy_bullet_is_armed(bullet) ? asset :
        asset|GARDEN_DRAW_FORM_BASE|(bullet && bullet->age>=GARDEN_BULLET_ARM_TICKS/2U ? GARDEN_DRAW_FORM_GROW:0U);
}

enum GardenFxFlags {
    GARDEN_FX_FLASH = 1U,
    GARDEN_FX_TRAIL = 2U,
    GARDEN_FX_RING = 4U,
    GARDEN_FX_ALL = 7U
};
typedef struct {
    int16_t x, y;
    uint16_t asset;
    uint8_t valid;
} GardenFxPoint;
typedef struct {
    /* Counts in the latest build; decorative drops are harmless budget cuts. */
    uint32_t flash_sprites, ghosts, rings, decorative_dropped, essential_dropped;
    uint32_t peak_draws;
    /* Lifetime frame counters are independent of UART reporting frequency. */
    uint32_t builds, flash_frames, trail_frames, ring_frames;
    uint32_t decorative_drop_total, essential_drop_total;
} GardenFxStats;
typedef struct {
    unsigned flags, initialized;
    uint32_t last_tick;
    uint8_t last_mode;
    GardenFxPoint current, previous[2]; /* previous[0] is the newer position. */
    GardenFxStats stats;
} GardenFx;

void garden_fx_reset(GardenFx *fx);
void garden_fx_set_flags(GardenFx *fx, unsigned flags);
/* Observe exactly once after each game logic step. Same-tick calls are safe. */
void garden_fx_observe(GardenFx *fx, const GardenGame *game);
/* Presentation heading from actual per-step velocity, nearest of the eight
 * frozen atlas directions (0=east,256=south). At rest use the stable original
 * firing direction, normally cord_angle. No game state or spin is changed. */
unsigned garden_fx_bullet_heading(int32_t vx,int32_t vy,unsigned fallback_angle);
/* Real enemy sprites use nearest64 velocity heading and the precision atlas;
 * the legacy eight-direction helper above remains source-compatible for
 * old host/benchmark callers. This builder still reserves one panel slot. */
unsigned garden_fx_build(GardenFx *fx, const GardenGame *game, GardenDraw *draws,
                         unsigned capacity, unsigned alpha_enabled);
const GardenFxStats *garden_fx_stats(const GardenFx *fx);
#endif
