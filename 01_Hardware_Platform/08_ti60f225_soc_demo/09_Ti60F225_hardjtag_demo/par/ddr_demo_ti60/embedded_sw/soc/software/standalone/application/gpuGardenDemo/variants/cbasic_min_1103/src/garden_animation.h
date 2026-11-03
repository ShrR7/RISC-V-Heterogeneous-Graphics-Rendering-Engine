/* SPDX-License-Identifier: GPL-3.0-or-later
 * Restricted presentation: original multicolour hazards and a small player-death burst.
 */
#ifndef GARDEN_ANIMATION_H
#define GARDEN_ANIMATION_H
#include "garden_core.h"
#include "garden_render.h"
#define GARDEN_ANIMATION_FRAGMENT_MAX 8U
#define GARDEN_ANIMATION_DEATH_TICKS 40U
#define GARDEN_ANIMATION_SHAKE_TICKS 4U
typedef struct {
    uint32_t death_triggers,death_frames,fragment_frames,fragment_dropped;
    uint32_t danger_bullet_frames,remapped_total,camera_frames,builds,peak_draws;
    /* remapped_total is retained for report compatibility; no palette remap. */
    uint32_t fragments,enemy_bullets;
    uint32_t bomb_frames,protection_frames;
    int32_t camera_x,camera_y;
} GardenAnimationStats;
typedef struct {
    unsigned initialized,paused,fraction_q8,death_active;
    uint32_t last_tick;
    int32_t last_player_x,last_player_y;
    int16_t death_x,death_y;
    uint8_t last_lives,last_mode,death_age;
    GardenAnimationStats stats;
} GardenAnimation;
void garden_animation_reset(GardenAnimation *animation);
/* Call after every real logic step. Reads lives/effect positions only, does
 * not consume RNG or change collision/HP/bullets/core clocks. Same tick is
 * idempotent; pause freezes; restart clears temporary death/camera state. */
void garden_animation_observe(GardenAnimation *animation,const GardenGame *game);
/* 0..255 sub-tick sample, clamped. Paused observer ignores fraction writes. */
void garden_animation_set_fraction(GardenAnimation *animation,unsigned fraction_q8);
/* Accept a FRESH complete draw list including HP bars and real hit marker.
 * Preserves every enemy bullet's original colour-family/rotation atlas ID;
 * only adds at most8small death fragments plus one local ring when capacity
 * remains. Thrown ordnance has its own stateless world view. Camera movement
 * is uniform across ALL field objects and only during protected death.
 * Caller keeps static UI outside this list; renderer retains fixed clipping.
 * Never call twice on the already-transformed list. */
unsigned garden_animation_apply(GardenAnimation *animation,const GardenGame *game,
                                GardenDraw *draws,unsigned count,unsigned capacity,
                                unsigned alpha_enabled);
const GardenAnimationStats *garden_animation_stats(const GardenAnimation *animation);
#endif
