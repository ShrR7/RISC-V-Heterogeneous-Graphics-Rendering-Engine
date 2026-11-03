/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only collision-centre marker and corroborated hit sparks.
 */
#ifndef GARDEN_FEEDBACK_H
#define GARDEN_FEEDBACK_H
#include "garden_fx.h"

#define GARDEN_FEEDBACK_SPARK_MAX 3U
#define GARDEN_FEEDBACK_SPARK_TICKS 6U

typedef struct {
    int32_t x, y, hp;
    uint8_t active, type, flash;
} GardenFeedbackTarget;
typedef struct {
    int32_t x, y;
    uint16_t age, damage;
    uint8_t active;
} GardenFeedbackBullet;
typedef struct {
    int16_t x, y;
    uint8_t active, age;
} GardenHitSpark;
typedef struct {
    /* hits counts target HP-change observations, not claimed damage numbers.
     * One observation may have several consumed-bullet spark candidates.
     */
    uint32_t hits, sparks_spawned, spark_candidates_dropped;
    uint32_t bullet_anchored_hits, fallback_hits;
    /* Latest complete draw-list build; marker is 0 or 1, sparks 0..3. */
    uint32_t sparks, marker, essential_dropped, decorative_dropped;
    uint32_t builds, spark_frames, marker_frames, peak_draws;
    uint32_t essential_drop_total, decorative_drop_total;
} GardenFeedbackStats;
typedef struct {
    unsigned initialized;
    uint32_t last_tick, last_score;
    uint8_t last_mode;
    GardenFeedbackTarget targets[GARDEN_MAX_ENEMIES+1U];
    GardenFeedbackBullet bullets[GARDEN_MAX_PLAYER_BULLETS];
    /* Newest first. At most three fixed 13x13 spark sprites; no heap. */
    GardenHitSpark sparks[GARDEN_FEEDBACK_SPARK_MAX];
    GardenFeedbackStats stats;
} GardenFeedback;

void garden_feedback_reset(GardenFeedback *feedback);
/* Call after every logic step. Same-tick calls are idempotent; pause freezes
 * sparks. Tick rollback or return to WAVES clears presentation history.
 */
void garden_feedback_observe(GardenFeedback *feedback,const GardenGame *game);
/* Builds the complete list using old FX, with admission priority:
 * necessary actors/bullets/effects, collision marker, hit sparks, old decor.
 * Painting remains old decor -> necessary objects -> sparks -> marker.
 * focus OR automatic demo shows the player's actual collision centre.
 * The core's enemy-bullet centre test is abs(dx/dy)<=9 px (19x19 inclusive),
 * not a new collision rule. Grace uses a distinct protected-player marker.
 */
unsigned garden_feedback_build(GardenFeedback *feedback,GardenFx *fx,
                               const GardenGame *game,GardenDraw *draws,
                               unsigned capacity,unsigned alpha_enabled,
                               unsigned focus);
const GardenFeedbackStats *garden_feedback_stats(const GardenFeedback *feedback);
#endif
