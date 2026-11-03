/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bullet presentation interpolation only; owns no gameplay or draw entries.
 */
#ifndef GARDEN_MOTION_H
#define GARDEN_MOTION_H
#include "garden_core.h"
#include "garden_render.h"
#define GARDEN_MOTION_Q4 16
#define GARDEN_MOTION_TELEPORT_PIXELS 64
/*12 bytes per fixed projectile slot. Coordinates retain1/16pixel precision;
 * the current core remains authoritative for activity, colour and heading. */
typedef struct {
    int16_t previous_x,previous_y,current_x,current_y;
    uint16_t age;
    uint8_t tag,smooth;
} GardenMotionSlot;
typedef struct {
    uint32_t builds,player_bullets,enemy_bullets,interpolated,snapped;
    uint32_t observations,epochs,match_rejected,range_rejected,teleports,reused;
} GardenMotionStats;
typedef struct {
    GardenMotionSlot player[GARDEN_MAX_PLAYER_BULLETS];
    GardenMotionSlot enemy[GARDEN_MAX_ENEMY_BULLETS];
    GardenMotionSlot pilot;
    GardenMotionStats stats;
    uint32_t last_tick;
    uint16_t fraction_q8;
    uint8_t initialized,paused,last_mode,last_lives;
} GardenMotion;
void garden_motion_reset(GardenMotion *motion);
/* Call after EVERY logic step, including every catchup step and pause events.
 * Repeated same-tick observation only updates pause state, not the histories. */
void garden_motion_observe(GardenMotion *motion,const GardenGame *game);
void garden_motion_set_fraction(GardenMotion *motion,unsigned fraction_q8);
/* Call after combat/HP-bar list composition and BEFORE camera/death fragments.
 * Matches the production PB/EB stable pool order, exact current pixel/ID and
 * arming opacity before replacing only x/y. Player body/banking and real focus
 * marker use the same interpolation fraction as the enemy white cores.
 * Count, order, asset, alpha are untouched. Death/respawn/teleport snap.
 * Return matching real projectile entries; not a new draw-list length.
 * Previous->current interpolation adds up to25ms visual latency. Core collision
 * and hit events remain at the current40Hz state; no extrapolation is used. */
unsigned garden_motion_apply_bullets(GardenMotion *motion,const GardenGame *game,
                                    GardenDraw *draws,unsigned count);
const GardenMotionStats *garden_motion_stats(const GardenMotion *motion);
#endif
