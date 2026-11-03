/* SPDX-License-Identifier: GPL-3.0-or-later
 * Stateless presentation of real thrown bombs, healing pickups and side guns. */
#ifndef GARDEN_ORDNANCE_VIEW_H
#define GARDEN_ORDNANCE_VIEW_H
#include "garden_core.h"
#include "garden_render.h"
/* Scene prefix helper: place these two beams after backgrounds, before real
 * actor/projectile/damage layers. Does not create a collision or change core. */
unsigned garden_ordnance_laser_count(const GardenGame *game);
unsigned garden_ordnance_laser_build(const GardenGame *game,GardenDraw *draws,
                                    unsigned capacity,unsigned alpha_enabled);
unsigned garden_ordnance_muzzle_build(const GardenGame *game,GardenDraw *draws,
                                     unsigned capacity,unsigned alpha_enabled);
/* Reserve these slots from earlier cosmetic builders; entities must remain
 * visible even when optional combat/background effects reach their limits. */
unsigned garden_ordnance_view_reserve(const GardenGame *game);
/* Main calls after animation and before stage death effects, replacing the
 * former pickup/flash loop. Inserts local visuals before player/enemy shots
 * and actual damage labels, keeping every existing real object/order intact.
 * No mutable storage, observer, RNG, raw framebuffer access or new history. */
unsigned garden_ordnance_view_build(const GardenGame *game,GardenDraw *draws,
                                   unsigned count,unsigned capacity,
                                   unsigned alpha_enabled);
/* Apply last, after every world list builder and interpolation. A short
 * damped bomb-impact shake moves actors, projectiles and labels together;
 * fixed cockpit panels are outside this list. Never changes game state. */
void garden_ordnance_camera_apply(const GardenGame *game,GardenDraw *draws,unsigned count);
#endif
