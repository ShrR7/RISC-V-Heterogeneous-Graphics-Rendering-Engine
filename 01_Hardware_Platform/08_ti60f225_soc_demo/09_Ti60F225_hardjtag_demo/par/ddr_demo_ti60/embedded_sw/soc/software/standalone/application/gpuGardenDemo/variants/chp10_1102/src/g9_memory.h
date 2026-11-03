/* SPDX-License-Identifier: GPL-3.0-or-later
 * Isolated Stage9 game DDR map. No hardware register writes in this helper.
 */
#ifndef G9_MEMORY_H
#define G9_MEMORY_H
#include <stdint.h>
#include "garden_core.h"
#include "garden_motion.h"
#include "garden_render.h"

#define G9_GUARD_BYTES 16U
#define G9_DDR_END 0x01000000U
#define G9_DESC_CAPACITY 2048U
#define G9_DESC_BYTES (G9_DESC_CAPACITY*16U)
#define G9_ATLAS_BYTES 16384U
#define G9_DESC0_ADDR 0x00810000U
#define G9_DESC1_ADDR 0x00820000U
#define G9_ATLAS0_ADDR 0x00830000U
#define G9_ATLAS1_ADDR 0x00840000U

/* Keep the former static-probe background range unallocated as well as the
 * old FB0/FB1,game atlas,batch list,panel and Alpha verification ranges. */
#define G9_OLD_BG_ADDR 0x00A00000U
#define G9_OLD_BG_BYTES (1280U*480U*2U)
#define G9_GAME_ADDR 0x00B40000U
#define G9_GAME_RESERVE_BYTES 0x00018000U
#define G9_MOTION_ADDR 0x00B60000U
#define G9_MOTION_RESERVE_BYTES 0x00008000U
#define G9_DRAWS_ADDR 0x00B70000U
#define G9_DRAWS_BYTES (GARDEN_DRAW_MAX*8U)
#define G9_HISTORY_ADDR 0x00B80000U
#define G9_HISTORY_BYTES (2U*GARDEN_DRAW_MAX*8U)
#define G9_ROUTE_ADDR 0x00B90000U
#define G9_ROUTE_BYTES 0x00010000U
/* CPU-only log storage, retained across game/T resets. */
#define G9_LOG_ADDR 0x00BB0000U
#define G9_LOG_BYTES (2880U+832U)

typedef struct { int16_t x,y; uint16_t w,h; } G9MemoryRect;
/* The engine-specific descriptor type can be cast from these four raw words;
 * this header deliberately does not duplicate the Stage9 driver's C type. */
typedef struct { uint32_t word[4]; } G9MemoryDescWords;

#define G9_GAME_PTR ((GardenGame *)(uintptr_t)G9_GAME_ADDR)
#define G9_MOTION_PTR ((GardenMotion *)(uintptr_t)G9_MOTION_ADDR)
#define G9_GAME (*G9_GAME_PTR)
#define G9_MOTION (*G9_MOTION_PTR)
/* Array lvalues preserve sizeof() and existing full-array memset semantics. */
#define G9_DRAWS (*(GardenDraw (*)[GARDEN_DRAW_MAX])(uintptr_t)G9_DRAWS_ADDR)
#define G9_HISTORY (*(G9MemoryRect (*)[2][GARDEN_DRAW_MAX])(uintptr_t)G9_HISTORY_ADDR)
#define G9_ROUTE (*(uint8_t (*)[G9_ROUTE_BYTES])(uintptr_t)G9_ROUTE_ADDR)
#define G9_DESC0 (*(G9MemoryDescWords (*)[G9_DESC_CAPACITY])(uintptr_t)G9_DESC0_ADDR)
#define G9_DESC1 (*(G9MemoryDescWords (*)[G9_DESC_CAPACITY])(uintptr_t)G9_DESC1_ADDR)
#define G9_ATLAS0 (*(uint16_t (*)[G9_ATLAS_BYTES/2U])(uintptr_t)G9_ATLAS0_ADDR)
#define G9_ATLAS1 (*(uint16_t (*)[G9_ATLAS_BYTES/2U])(uintptr_t)G9_ATLAS1_ADDR)

/* Each actual payload has four32-bit sentinels immediately before and after
 * it. Guard checks invalidate the write-through CPU cache before reading.
 * init writes guards only; it never resets descriptors or atlas data.
 * reset clears complete CPU-owned objects/arrays only, preserving guards and
 * GPU input buffers. Call at bring-up with Stage9 safely disabled, not while
 * another owner is using the route/draw/history storage. Existing garden_init
 * still clears sizeof(GardenGame),including all2000 embedded bullet slots. */
void g9_memory_guard_init(void);
unsigned g9_memory_guard_check(void);
void g9_memory_reset(void);

#endif
