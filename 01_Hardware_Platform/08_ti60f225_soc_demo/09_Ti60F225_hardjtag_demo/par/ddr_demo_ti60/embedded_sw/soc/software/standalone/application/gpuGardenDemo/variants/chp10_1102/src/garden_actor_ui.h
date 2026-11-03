/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only bank poses and HP bars tied to actual actor HP.
 */
#ifndef GARDEN_ACTOR_UI_H
#define GARDEN_ACTOR_UI_H
#include "garden_core.h"
#include "garden_render.h"

#define GARDEN_ACTOR_UI_ENEMY_BAR_WIDTH 40U
#define GARDEN_ACTOR_UI_BOSS_BAR_WIDTH 128U
#define GARDEN_ACTOR_UI_BAR_HEIGHT 8U
#define GARDEN_ACTOR_UI_BAR_GAP 4U
typedef struct {
    uint32_t bank_changes, bank_frames, player_frames, bar_frames;
    uint32_t bars, enemy_bars, boss_bars, bar_drop_total;
    int32_t bank;
} GardenActorUiStats;
typedef struct {
    unsigned initialized;
    uint32_t last_tick;
    uint8_t last_mode;
    int8_t bank;
    GardenActorUiStats stats;
} GardenActorUi;

void garden_actor_ui_reset(GardenActorUi *ui);
/* Same-tick / paused samples cannot animate. Tick rollback resets the pose;
 * each advancing 25ms logic sample moves at most one of the five bank poses.
 * Banking is drawing only: no player position, speed or collision writes.
 */
void garden_actor_ui_observe(GardenActorUi *ui,const GardenGame *game);
unsigned garden_actor_ui_player_asset(const GardenActorUi *ui,const GardenGame *game);
unsigned garden_actor_ui_bar_count(const GardenGame *game);
/* Model anchor to HP-strip top; also shared by killing-hit popup fallback. */
unsigned garden_actor_ui_bar_offset(unsigned asset);
/* Returns 1 for a live target whose actual sprite bounding box intersects
+ * the field; fully offscreen actors never leave isolated border bars. Virtual
+ * top-left bar, with true HP fill. Slot
 * 0..7 = enemy, 8 = Boss. Shared by popup avoidance and the draw builder.
 */
unsigned garden_actor_ui_bar_geometry(const GardenGame *game,unsigned slot,GardenDraw *draw);
/* Replace the last matching real-player entry only. Old trail samples are
 * retained, as are the actual body/marker coordinates and all game state.
 */
unsigned garden_actor_ui_apply(GardenActorUi *ui,const GardenGame *game,GardenDraw *draws,unsigned count);
unsigned garden_actor_ui_append_bars(GardenActorUi *ui,const GardenGame *game,
                                    GardenDraw *draws,unsigned count,unsigned capacity);
const GardenActorUiStats *garden_actor_ui_stats(const GardenActorUi *ui);
#endif
