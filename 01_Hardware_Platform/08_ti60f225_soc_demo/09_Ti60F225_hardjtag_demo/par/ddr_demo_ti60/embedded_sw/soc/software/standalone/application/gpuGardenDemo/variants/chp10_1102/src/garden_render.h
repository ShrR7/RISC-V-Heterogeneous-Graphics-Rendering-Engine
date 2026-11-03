/* Garden board renderer, GPL-3.0-or-later. Reuses the frozen Stage 6 API. */
#pragma once
#include <stdint.h>
#define GARDEN_DRAW_MAX 2240U
/* Virtual composite damage: lower 15 bits encode positive effective HP loss.
 * Coordinates are the top-left of the minus/digits rectangle in logical field
 * pixels. One composite occupies one dirty-history slot, no new atlas bytes. */
#define GARDEN_DRAW_SCORE_BASE 0x8000U
/* Compatibility score names now paint HP-loss labels, never awarded points. */
#define GARDEN_DRAW_BAR_BASE 0x4000U
#define GARDEN_DRAW_BAR_BOSS 0x0100U
#define GARDEN_DRAW_BAR_FILL_MASK 0x00FFU
/* Virtual bank: ((bank+2)<<2)|thruster_frame; bank -2..2, frame 0..2. */
#define GARDEN_DRAW_BANK_BASE 0x2000U
#define GARDEN_DRAW_LASER_BASE 0x1000U
#define GARDEN_DRAW_LASER_ACTIVE 0x0020U
#define GARDEN_DRAW_LASER_FADE 0x0040U
#define GARDEN_DRAW_LASER_FRAME 0x0080U
#define GARDEN_DRAW_LASER_SCROLL_MASK 0x0F00U
#define GARDEN_DRAW_LASER_SCROLL_SHIFT 8U
#define GARDEN_DRAW_FORM_BASE 0x0800U
#define GARDEN_DRAW_FORM_GROW 0x0400U
#define GARDEN_DRAW_FORM_ASSET_MASK 0x03FFU
static inline unsigned garden_draw_form_source(unsigned asset)
{ return (asset&0xF800U)==GARDEN_DRAW_FORM_BASE ? asset&GARDEN_DRAW_FORM_ASSET_MASK:asset; }
/* Low5bits are the bright collision-core pixel width (13 for +/-6px).
 * x is its centre, y the first damaging row. A single dirty-history entry
 * covers the repeated real atlas beam texture down to the field bottom.
 * WARN uses a one-pixel low-alpha centre column and is never damaging. */
#define GARDEN_DRAW_LASER_WIDTH_MASK 0x001FU
#define GARDEN_DRAW_SCORE_MAX 0x7FFFU
#define GARDEN_DRAW_SCORE_HEIGHT 16U
#define GARDEN_DRAW_SCORE_PLUS_ADVANCE 12U
#define GARDEN_DRAW_SCORE_DIGIT_ADVANCE 16U
unsigned garden_render_score_width(unsigned value);
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
    /* commands counts GPU submissions (one for each list), not instances.
     * A successful frame matches actual hardware starts. Error frames cannot
     * be used for performance comparisons. CPU retains code-native counts. */
    uint32_t batch_commands, batch_instances;
    uint32_t batch_fill_commands, batch_blit_commands;
} GardenRenderStats;
void garden_render_init(void);
/* External test owns both surfaces; invalidate only histories/UI caches. */
int garden_render_surface_reset(void);
/* Battlefield-only tint; each physical buffer is repainted on a change and
 * later dirty erases restore the same colour. Zero restores black/stars. */
void garden_render_field_color(uint16_t color);
void garden_render_mode(int cpu);
int garden_render_is_cpu(void);
/* Batch only within a frame; changing mode synchronously drains any queue.
 * Default single, for an identical game/atlas performance comparison. */
void garden_render_batch_mode(int enabled);
int garden_render_is_batch(void);
void garden_render_alpha_support(int supported);
int garden_render_alpha_supported(void);
int garden_render_alpha_enabled(void);
int garden_render_alpha_toggle(void);
unsigned garden_render_alpha_level(void);
void garden_render_alpha_cycle(void);
/* Optional diagnostic panel; showcase effects use Alpha without this label. */
void garden_render_panel_visible(unsigned visible);
int garden_render_panel_is_visible(void);
int garden_render_frame(const GardenDraw *draws, unsigned count);
void garden_render_text(int x, int y, const char *text);
enum GardenTextStyle {
    GARDEN_TEXT_SMALL_WHITE, GARDEN_TEXT_SMALL_CYAN,
    GARDEN_TEXT_LARGE_WHITE, GARDEN_TEXT_LARGE_CYAN
};
void garden_render_text_style(int x, int y, const char *text, unsigned style);
void garden_render_box(int x, int y, int w, int h, uint16_t color);
const GardenRenderStats *garden_render_stats(void);
int garden_render_key_probe(void);
