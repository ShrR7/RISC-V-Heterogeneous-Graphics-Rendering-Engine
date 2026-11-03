/* SPDX-License-Identifier: GPL-3.0-or-later
 * Platform-independent, paced state clearing for the diagnostic game copy.
 */
#ifndef GARDEN_CLEAR_H
#define GARDEN_CLEAR_H

#include <stddef.h>
#include <stdint.h>

#define GARDEN_CLEAR_CHUNK_BYTES 64U

typedef void (*GardenClearYield)(void);
typedef struct GardenClearStats {
    uint32_t calls,bytes,chunks,yields,max_chunk;
} GardenClearStats;

/* The default hook is NULL. A board hook may fence and wait without changing
 * game state. All calls are synchronous and non-reentrant. The hook runs only
 * between chunks, never after the final chunk; max_chunk describes one clear
 * operation, not every other write the CPU/DDR controller may combine. This
 * pacing is a diagnostic mitigation, not a display-deadline guarantee.
 */
void garden_clear_set_yield(GardenClearYield yield);
void garden_clear_zero(void *destination,size_t bytes);
const GardenClearStats *garden_clear_get_stats(void);
void garden_clear_reset_stats(void);

#endif
