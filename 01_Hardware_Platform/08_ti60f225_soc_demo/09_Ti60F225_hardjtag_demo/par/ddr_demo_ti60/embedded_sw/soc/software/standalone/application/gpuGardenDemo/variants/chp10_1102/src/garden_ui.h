/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

/* UI data has no MMIO dependencies: the host preview uses the same layout. */
typedef struct {
    const char *state;
    uint32_t score, lives, bullets, peak, fps10, render_us, logic_us;
    uint32_t gpu_bench_us, cpu_bench_us, errors;
    uint32_t boss_hp, boss_max_hp, boss_attack;
    uint32_t high_score,bombs,graze,items,phase;
    unsigned diagnostics;
    unsigned cpu, demo, alpha_supported, alpha_enabled, alpha_level, boss_active;
} GardenHud;

#define GARDEN_UI_BACKGROUND 0x0843U
#define GARDEN_UI_PANEL      0x1087U
#define GARDEN_UI_EDGE       0x19A8U
#define GARDEN_UI_MUTED      0x224FU
#define GARDEN_UI_CYAN       0x47FFU
#define GARDEN_UI_MAGENTA    0xD21BU
#define GARDEN_UI_GOLD       0xFDC9U

void garden_ui_static(void);
void garden_ui_stars(int x,int y,int w,int h);
uint16_t garden_ui_alpha_pixel(unsigned x,unsigned y);
void garden_ui_hud(const GardenHud *hud);
/* Separate histories for the two physical framebuffers. Reset after rebuilding
 * the static UI or changing framebuffer contents outside these HUD helpers.
 */
void garden_ui_hud_cached(const GardenHud *hud,unsigned buffer_index);
/* Compatibility no-op: HP is displayed only above actors in the playfield. */
void garden_ui_boss_cached(const GardenHud *hud,unsigned buffer_index);
/* Score-only refresh follows actual score changes independently of the slow
 * performance HUD. Per-buffer cache never consumes the initial full draw.
 * Caller must reset all HUD caches if renderer error counters change. */
void garden_ui_score_cached(const GardenHud *hud,unsigned buffer_index);
void garden_ui_hud_reset(void);
