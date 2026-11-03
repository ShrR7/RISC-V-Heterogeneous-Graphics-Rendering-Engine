/* Garden board renderer, GPL-3.0-or-later. Reuses the frozen Stage 6 API. */
#pragma once
#include <stdint.h>
#define GARDEN_DRAW_MAX 240U
#define GARDEN_X0 320
#define GARDEN_Y0 120
#define GARDEN_WIDTH 640
#define GARDEN_HEIGHT 480
/* alpha is the PR #32 global value: 0..32, with 32 opaque. */
typedef struct { int16_t x, y; uint16_t asset; uint8_t alpha; } GardenDraw;
typedef struct {
    uint32_t errors, bad_targets, atlas_bounds, commands;
    uint32_t sprites;
    uint32_t alpha_commands, alpha_sprites;
} GardenRenderStats;
void garden_render_init(void);
void garden_render_mode(int cpu);
int garden_render_is_cpu(void);
void garden_render_alpha_support(int supported);
int garden_render_alpha_supported(void);
int garden_render_alpha_enabled(void);
int garden_render_alpha_toggle(void);
unsigned garden_render_alpha_level(void);
void garden_render_alpha_cycle(void);
int garden_render_frame(const GardenDraw *draws, unsigned count);
void garden_render_text(int x, int y, const char *text);
void garden_render_box(int x, int y, int w, int h, uint16_t color);
const GardenRenderStats *garden_render_stats(void);
int garden_render_key_probe(void);
