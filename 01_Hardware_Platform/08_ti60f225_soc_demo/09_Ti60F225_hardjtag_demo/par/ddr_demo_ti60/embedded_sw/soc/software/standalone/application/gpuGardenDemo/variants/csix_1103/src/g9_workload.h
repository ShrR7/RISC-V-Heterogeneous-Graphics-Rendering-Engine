/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit injected dynamic gameplay stress, never a natural-level claim.
 */
#ifndef G9_WORKLOAD_H
#define G9_WORKLOAD_H
#include "garden_core.h"
typedef struct {
    uint32_t generation,target,calls,steps,observations;
    uint32_t replenished,teleports,skipped_paused,skipped_end,restart_cancelled;
    uint32_t last_live,min_live,max_live,above_target,below_target;
} G9WorkloadStats;
/* Return0 on success,-1 for null game or unsupported target,with no changes.
 * Allowed targets:0,128,512,1024,1500,2000. Every accepted selection calls the
 * normal garden_init and preserves the actual prior session best.
 * Nonzero targets construct a real Boss in its ordinary capacity WAIT state;
 * >=128 injected bullets naturally exceed the unchanged80-bullet admission
 * budget. No collision,AI,damage,bomb,laser or scoring code is bypassed.
 * The initial game counters are left to the ordinary next garden_step pass;
 * last_live/min_live/max_live count actual active slots,never requested slots.
 */
int g9_workload_set(GardenGame *game,unsigned count);
/* Call immediately before an ordinary garden_step. Paused/end states do not
 * inject. A normal restart/seed change cancels injection instead of undoing R.
 * Inactive owned slots are replenished; boundary wraps are counted explicitly.
 */
void g9_workload_before_step(GardenGame *game);
/* Call after garden_step to include the true post-collision/end-state count.
 * No game fields are written. It also observes/cancels a normal restart.
 */
void g9_workload_after_step(const GardenGame *game);
unsigned g9_workload_get_target(void);
const G9WorkloadStats *g9_workload_stats(void);
#endif
