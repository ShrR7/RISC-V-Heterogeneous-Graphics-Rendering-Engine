/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation of explicit, effective-hit events from the core.
 */
#ifndef GARDEN_COMBAT_H
#define GARDEN_COMBAT_H
#include "garden_scene.h"

#ifndef GARDEN_DRAW_SCORE_BASE
#define GARDEN_DRAW_SCORE_BASE 0x8000U
#endif
#define GARDEN_COMBAT_IMPACT_MAX 3U
#define GARDEN_COMBAT_KILL_MAX 2U
#define GARDEN_COMBAT_POPUP_MAX GARDEN_COMBAT_EVENT_MAX
#define GARDEN_COMBAT_FLASH_TICKS 6U
#define GARDEN_COMBAT_IMPACT_TICKS 6U
#define GARDEN_COMBAT_KILL_TICKS 12U
#define GARDEN_COMBAT_POPUP_TICKS 24U
#define GARDEN_COMBAT_POPUP_RISE 32U
#define GARDEN_COMBAT_POPUP_JITTER 0U /* side anchors do not use bullet-centre jitter */
#define GARDEN_COMBAT_GROUP_TICKS 6U

typedef struct {
    int16_t x,y;
    uint8_t active,age;
} GardenCombatImpact;
typedef struct {
    int16_t x,y;
    uint16_t damage;
    uint8_t active,age,slot,killed;
    uint8_t group_age,motion_age,target_type; /* group_age reserved0; motion_age mirrors age */
    int32_t target_x,target_y;
    uint32_t spawn_sequence;
    uint8_t anchor_ready,anchor_visible,rise_budget,anchor_policy;
} GardenCombatPopup;
typedef struct {
    int32_t x,y;
    uint8_t remaining,type;
} GardenCombatFlash;
typedef struct {
    /* Only successfully consumed explicit events contribute to observed
     * scores and effective HP damage. damage_display_clamped counts damage
     * units omitted only by the 15-bit presentation value limit. Replacements are bounded presentation loss, not core loss.
     */
    uint32_t events_observed,kills_observed,score_observed,event_rejected,event_gaps;
    uint32_t damage_observed,damage_display_clamped;
    uint32_t impacts_spawned,kill_rings_spawned,popups_spawned,popups_merged;
    uint32_t popups_updated,popup_groups_replaced,popup_layout_dropped; /* first two reserved0 */
    uint32_t popup_peak_active,popup_pool_exhausted,popup_stale_cleared;
    uint32_t popup_anchors_chosen,popup_anchor_failures;
    uint32_t impact_replaced,kill_ring_replaced,popup_replaced;
    uint32_t impacts,impact_layers,kill_rings,popups,held_flashes,marker;
    uint32_t essential_dropped,decorative_dropped;
    uint32_t builds,impact_frames,kill_frames,popup_frames,flash_frames,marker_frames;
    uint32_t peak_draws,essential_drop_total,decorative_drop_total;
} GardenCombatStats;
typedef struct {
    unsigned initialized,enabled,render_fraction_q8;
    uint32_t last_tick,last_emitted_total;
    uint8_t last_mode;
    uint8_t target_was_active[GARDEN_MAX_ENEMIES+1U];
    GardenCombatFlash flash[GARDEN_MAX_ENEMIES+1U];
    GardenCombatImpact impacts[GARDEN_COMBAT_IMPACT_MAX];
    GardenCombatImpact kill_rings[GARDEN_COMBAT_KILL_MAX];
    GardenCombatPopup popups[GARDEN_COMBAT_POPUP_MAX];
    GardenCombatStats stats;
} GardenCombat;

void garden_combat_reset(GardenCombat *combat);
/* Fraction of the current logic step, 0..255, for sub-tick motion/fading.
 * The caller keeps it unchanged while paused; core/events are never changed. */
void garden_combat_set_render_fraction(GardenCombat *combat,unsigned fraction_q8);
/* Toggle presentation only. Disabling clears held visuals, and observing
 * while disabled consumes events without replaying them on re-enable.
 */
void garden_combat_set_enabled(GardenCombat *combat,unsigned enabled);
/* Call after each logic step. Only combat.tick==game.tick is fresh. The
 * first observation may consume that step's events; same tick is idempotent.
 * Hits in this same step and target share one summed HP-damage label.
 * Later steps and different targets retain independent labels.
 * Pause freezes ages; restart/epoch changes clear temporary presentation.
 */
void garden_combat_observe(GardenCombat *combat,const GardenGame *game);
/* Actors and actual collision marker always win admission. Event damage/kill
 * feedback then wins before old scene decorations. Strong impacts and damage
 * composites paint after projectiles; marker paints last. Damage draws are a
 * single history entry per target per logic step; its damage is the sum
 * of valid effective-hit events. Later-step labels may overlap.
 * Birth anchors are centred above the model/HP bar, with a once-set0..32px
 * rise budget; later frames never relayout or clamp the label. The fixed event-sized pool replaces the oldest presentation when full, and assets
 * encode actual effective HP damage, excluding overkill and score rewards.
 * Independent game score and total observed HP accounting remain unchanged.
 */
unsigned garden_combat_build(GardenCombat *combat,GardenScene *scene,
                            GardenFeedback *feedback,GardenFx *fx,
                            const GardenGame *game,GardenDraw *draws,
                            unsigned capacity,unsigned alpha_enabled,unsigned focus);
/* Pure readout for identity-bound diagnostics: returns the current draw for
 * one active anchored label; does not reserve/admit it or change statistics. */
unsigned garden_combat_popup_draw(const GardenCombat *combat,unsigned popup_index,
                                  unsigned alpha_enabled,GardenDraw *draw);
const GardenCombatStats *garden_combat_stats(const GardenCombat *combat);
#endif
