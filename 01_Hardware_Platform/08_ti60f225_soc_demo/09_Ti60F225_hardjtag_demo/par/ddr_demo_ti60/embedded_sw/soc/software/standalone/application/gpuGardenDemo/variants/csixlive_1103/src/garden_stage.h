/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only entrance, staged Boss death and cached result card.
 * Reads reward statistics; never writes core or accesses framebuffer MMIO.
 */
#ifndef GARDEN_STAGE_H
#define GARDEN_STAGE_H
#include "garden_core.h"
#include "garden_render.h"

#define GARDEN_STAGE_DEATH_TICKS 100U
#define GARDEN_STAGE_SCORE_TICKS 80U
#define GARDEN_STAGE_PROMPT_TICKS 220U
#define GARDEN_STAGE_DEATH_DRAW_MAX 12U

enum GardenStageKind {
    GARDEN_STAGE_FOOTER,
    GARDEN_STAGE_WARNING,
    GARDEN_STAGE_VICTORY,
    GARDEN_STAGE_RESULTS
};
typedef struct {
    uint32_t boss_entries, victories, restarts, banner_redraws;
    /* Logic observations in an active Boss entrance's first 64 ticks.
     * A paused/same-tick observation never increments this counter.
     */
    uint32_t boss_fade_samples;
    uint32_t death_frames, death_draws, modal_redraws, modal_updates, modal_clears;
} GardenStageStats;
typedef struct {
    uint32_t score;
    uint8_t valid, kind, pulse, progress, demo;
} GardenStageCache;
typedef struct {
    unsigned enabled, initialized;
    uint32_t last_tick, boss_entry_tick, victory_tick;
    uint32_t last_score, victory_base_score;
    uint8_t last_mode, boss_entry_valid;
    uint8_t modal_buffers;
    int16_t death_x, death_y;
    GardenStageCache cache[2];
    GardenStageStats stats;
} GardenStage;

/* reset enables presentation and clears its lifetime counters. */
void garden_stage_reset(GardenStage *stage);
void garden_stage_set_enabled(GardenStage *stage, unsigned enabled);
/* Call after each logic step; repeated same-tick calls are idempotent. */
void garden_stage_observe(GardenStage *stage, const GardenGame *game);
/* Only the detected WAVES->BOSS entrance fades: age 0..63 maps 8..32.
 * No game attack, movement, collision, or victory timing is altered.
 */
unsigned garden_stage_boss_alpha(const GardenStage *stage,
                               const GardenGame *game, unsigned alpha_enabled);
/* Draws the footer x=[504,948),y=[634,678) during play. After the100tick
 * Boss death it draws a cached result card strictly inside the playfield
 * x=[320,960),y=[120,600), using the renderer's current writable target.
 * Returns 1 when drawn, 0 when cache remains current, -1 for NULL arguments,
 * or -2 if drawing added renderer errors, bad targets, or atlas violations.
 * A failed draw invalidates both histories and must be retried in full.
 * Cache indices are the physical framebuffer, not the render frame number.
 * Unknown indices redraw conservatively and invalidate both histories.
 */
int garden_stage_draw(GardenStage *stage, const GardenGame *game,
                      unsigned buffer_index);
/* Scene draw list is empty after the death presentation. Call prepare_frame
 * after render_begin but BEFORE garden_render_frame to clear any old modal on
 * restart, then stage_draw AFTER garden_render_frame. Both calls target only
 * the current writable buffer selected by the caller's renderer. */
unsigned garden_stage_modal_active(const GardenStage *stage,const GardenGame *game);
/* Presentation-only credit: pre-kill score during the death, then an80tick
 * roll to the live true score. Main HUD and modal must share this getter. */
uint32_t garden_stage_visible_score(const GardenStage *stage,const GardenGame *game);
int garden_stage_prepare_frame(GardenStage *stage,const GardenGame *game,
                               unsigned buffer_index);
unsigned garden_stage_append_effects(GardenStage *stage,const GardenGame *game,
                                    GardenDraw *draws,unsigned count,unsigned capacity,
                                    unsigned alpha_enabled);
void garden_stage_ui_reset(GardenStage *stage);
const GardenStageStats *garden_stage_stats(const GardenStage *stage);
#endif
