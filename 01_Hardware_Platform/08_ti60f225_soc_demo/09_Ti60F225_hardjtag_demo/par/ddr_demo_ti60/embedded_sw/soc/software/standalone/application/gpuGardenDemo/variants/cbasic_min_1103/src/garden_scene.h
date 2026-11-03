/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only mechanical Boss, timed anticipation and garden parallax.
 */
#ifndef GARDEN_SCENE_H
#define GARDEN_SCENE_H
#include "garden_feedback.h"

#define GARDEN_SCENE_CHARGE_TICKS 20U
#define GARDEN_SCENE_BURST_TICKS 4U
#define GARDEN_SCENE_BACKDROP_MAX 8U
#define GARDEN_SCENE_CLOSE_TICKS 16U

/* New cruiser components in logical-field pixels. Body is the true Boss
 * anchor; core/nose and muzzle-anchored pods use core.h's canonical offsets.
 * Cosmetic pod lag is disabled while the real laser is active. Union bounds
 * include the body and currently visible shell; right/bottom are exclusive.
 */
typedef struct {
    GardenDraw wings[2],core,cue;
    unsigned cue_visible,open_q8;
    int16_t left,top,right,bottom;
} GardenSceneBossPose;

typedef struct {
    /* Latest complete draw-list build. Missing decorations are budget cuts. */
    uint32_t wings, core, charge, backgrounds, charge_countdown, burst_active;
    uint32_t essential_dropped, decorative_dropped, builds, peak_draws;
    uint32_t wing_frames, core_frames, charge_frames, burst_frames, background_frames;
    uint32_t essential_drop_total, decorative_drop_total;
    /* Actual core attack-counter increments, not bullet counts or new attacks. */
    uint32_t attack_shots_observed, attack_changes_observed, half_hp_transitions;
    uint32_t charge_windows;
} GardenSceneStats;

typedef struct {
    unsigned enabled, initialized;
    uint32_t last_tick, last_shots[3];
    uint16_t charge_countdown;
    uint8_t last_mode, last_boss_active, last_attack, low_hp, burst_ticks;
    uint16_t render_fraction,last_open_q8,wait_from_q8;
    uint32_t wait_started_tick;
    int16_t boss_x_history[4],boss_y_history[4];
    uint8_t paused,last_rhythm_state,history_valid;
    GardenSceneStats stats;
} GardenScene;

void garden_scene_reset(GardenScene *scene);
/* Observes after each logic step; no core, RNG, MMIO or heap writes. Same tick
 * is idempotent. Restart/epoch changes clear temporary cue and burst state.
 */
void garden_scene_observe(GardenScene *scene,const GardenGame *game);
void garden_scene_set_enabled(GardenScene *scene,unsigned enabled);
/* Fraction0..255 of the current25ms logic interval, supplied by the display
 * loop. Pause keeps the previous fraction. Changes never mutate the game.
 */
void garden_scene_set_render_fraction(GardenScene *scene,unsigned q8);
unsigned garden_scene_boss_pose(const GardenScene *scene,const GardenGame *game,
                               GardenSceneBossPose *pose);
/* Essential actors -> marker/sparks -> mechanical Boss -> old FX -> backdrop
 * is admission priority. Painting places scenery behind actors; reactor core
 * overlays only the Boss body, before all projectile cores and marker/sparks.
 * Boss decorations are visual and never enlarge the original collision box.
 */
unsigned garden_scene_build(GardenScene *scene,GardenFeedback *feedback,GardenFx *fx,
                           const GardenGame *game,GardenDraw *draws,
                           unsigned capacity,unsigned alpha_enabled,unsigned focus);
const GardenSceneStats *garden_scene_stats(const GardenScene *scene);
#endif
