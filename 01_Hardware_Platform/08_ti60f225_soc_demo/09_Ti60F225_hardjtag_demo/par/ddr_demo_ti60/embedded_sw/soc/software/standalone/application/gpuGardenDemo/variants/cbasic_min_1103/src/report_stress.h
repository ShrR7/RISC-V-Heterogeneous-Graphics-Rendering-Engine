/* GPL-3.0-or-later. Board-only, common-task report pressure scenes. */
#pragma once
#include <stdint.h>
/* backend: 0 Stage9+legacy, 1 CPU software, 2 legacy GPU.
 * scene: 0 accepted 91-ray fan, 1 uniform grid, 2 perimeter, 3 Alpha16 pile.
 * Non-fan GPU scenes use actual backend 2 and explicitly report that choice.
 * Caller suspends game logic, then rebuilds both game surfaces on return.
 * Does not touch the game/motion objects or the Flash contents. */
int report_stress_run(unsigned backend,unsigned scene,unsigned seconds);
