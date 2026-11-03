/* SPDX-License-Identifier: GPL-3.0-or-later
 * Garden of Coloured Lights: Ti60 demonstration renderer.
 * Dirty rectangles are stored separately for the two physical framebuffers.
 */
#include "garden_render.h"
#include "garden_assets.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"
#include "gpu_display.h"
#include "vexriscv.h"
#include <string.h>

typedef struct { int16_t x, y; uint16_t w, h; } Rect;
static Rect history[2][GARDEN_DRAW_MAX];
static unsigned history_count[2];
static unsigned initialized[2];
static int use_cpu;
static int alpha_supported, alpha_enabled;
static unsigned panel_alpha;
/* Separate from the atlas and the last 2 KiB used by gpu_alpha_verify. */
#define PANEL_SOURCE (ASSET_BASE_ADDR + ASSET_SIZE_BYTES - 8192U)
#define PANEL_WIDTH 96U
#define PANEL_HEIGHT 32U
static GardenRenderStats stats;

static void record_result(int rc)
{
    if (rc != GPU_OK) stats.errors++;
}

void garden_render_box(int x, int y, int w, int h, uint16_t color)
{
    uint32_t dst;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)DISPLAY_WIDTH) w = (int)DISPLAY_WIDTH - x;
    if (y + h > (int)DISPLAY_HEIGHT) h = (int)DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    dst = display_back_addr() + (uint32_t)y * DISPLAY_STRIDE_BYTES + (uint32_t)x * 2U;
    if (display_check_render_target(dst) != GPU_OK) { stats.bad_targets++; return; }
    if (use_cpu) {
        for (int row = 0; row < h; row++) {
            volatile uint16_t *out = (volatile uint16_t *)(uintptr_t)(dst + (uint32_t)row * DISPLAY_STRIDE_BYTES);
            for (int col = 0; col < w; col++) out[col] = color;
        }
        stats.commands++;
    } else {
        /* Keep even a full-screen clear below the engine's per-command watchdog. */
        for (int row = 0; row < h; row += 16) {
            unsigned band = (unsigned)(h-row);
            if (band > 16U) band = 16U;
            record_result(gpu_fill(dst+(uint32_t)row*DISPLAY_STRIDE_BYTES,(unsigned)w,band,DISPLAY_STRIDE_BYTES,color));
            stats.commands++;
        }
    }
}

static int sprite(unsigned id, int x, int y, int in_field, unsigned alpha, Rect *painted)
{
    const garden_sprite_t *s = garden_asset(id);
    int sx = 0, sy = 0, w, h;
    int xmin = in_field ? GARDEN_X0 : 0;
    int ymin = in_field ? GARDEN_Y0 : 0;
    int xmax = in_field ? GARDEN_X0+GARDEN_WIDTH : (int)DISPLAY_WIDTH;
    int ymax = in_field ? GARDEN_Y0+GARDEN_HEIGHT : (int)DISPLAY_HEIGHT;
    uint32_t src, dst;
    uint64_t source_end;
    if (alpha > 32U) { stats.errors++; return 0; }
    if (!alpha_enabled) alpha=32U;
    if (!s || !s->width || !s->height || s->stride < s->width*2U) { stats.atlas_bounds++; return 0; }
    x -= s->anchor_x; y -= s->anchor_y;
    w = s->width; h = s->height;
    if (x < xmin) { sx=xmin-x; w-=sx; x=xmin; }
    if (y < ymin) { sy=ymin-y; h-=sy; y=ymin; }
    if (x+w > xmax) w=xmax-x;
    if (y+h > ymax) h=ymax-y;
    if (w <= 0 || h <= 0) return 0;
    src = ASSET_BASE_ADDR + s->offset + (uint32_t)sy*s->stride + (uint32_t)sx*2U;
    source_end = (uint64_t)src + (uint64_t)(h-1)*s->stride + (unsigned)w*2U;
    if (source_end > (uint64_t)ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES || (src & 1U)) { stats.atlas_bounds++; return 0; }
    dst = display_back_addr() + (uint32_t)y*DISPLAY_STRIDE_BYTES + (uint32_t)x*2U;
    if (display_check_render_target(dst) != GPU_OK) { stats.bad_targets++; return 0; }
    if (use_cpu) {
        for (int row=0; row<h; row++) {
            const volatile uint16_t *in = (const volatile uint16_t *)(uintptr_t)(src+(uint32_t)row*s->stride);
            volatile uint16_t *out = (volatile uint16_t *)(uintptr_t)(dst+(uint32_t)row*DISPLAY_STRIDE_BYTES);
            for (int col=0; col<w; col++) {
                uint16_t v=in[col];
                if (v!=GARDEN_KEY_COLOR) out[col]=alpha==32U ? v : gpu_alpha_rgb565(v,out[col],alpha);
            }
        }
        stats.commands++;
        if (alpha<32U) stats.alpha_commands++;
    } else {
        for (int row=0; row<h; row+=16) {
            unsigned band=(unsigned)(h-row); if (band>16U) band=16U;
            if (alpha<32U) {
                record_result(gpu_alpha_bitblt(src+(uint32_t)row*s->stride,dst+(uint32_t)row*DISPLAY_STRIDE_BYTES,(unsigned)w,band,s->stride,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR,alpha));
                stats.alpha_commands++;
            } else {
                record_result(gpu_bitblt(src+(uint32_t)row*s->stride,dst+(uint32_t)row*DISPLAY_STRIDE_BYTES,(unsigned)w,band,s->stride,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR));
            }
            stats.commands++;
        }
    }
    if (painted) { painted->x=(int16_t)x; painted->y=(int16_t)y; painted->w=(uint16_t)w; painted->h=(uint16_t)h; }
    return 1;
}

void garden_render_text(int x, int y, const char *text)
{
    int start=x;
    while (*text) {
        int id=garden_glyph(*text);
        if (*text=='\n') { x=start; y+=16; }
        else { if (id>=0) sprite((unsigned)id,x,y,0,32U,0); x+=8; }
        text++;
    }
}

void garden_render_init(void)
{
    memset(history_count,0,sizeof(history_count));
    memset(initialized,0,sizeof(initialized));
    memset(&stats,0,sizeof(stats));
    use_cpu=0;
    alpha_supported=alpha_enabled=0;
    panel_alpha=16U;
}
void garden_render_mode(int cpu) { use_cpu=cpu!=0; }
int garden_render_is_cpu(void) { return use_cpu; }
int garden_render_alpha_supported(void) { return alpha_supported; }
int garden_render_alpha_enabled(void) { return alpha_enabled; }
unsigned garden_render_alpha_level(void) { return panel_alpha; }
int garden_render_alpha_toggle(void)
{
    if (!alpha_supported) return GPU_ESTATE;
    alpha_enabled=!alpha_enabled;
    return GPU_OK;
}
void garden_render_alpha_cycle(void)
{
    if (alpha_supported) panel_alpha=panel_alpha==24U ? 8U : panel_alpha+8U;
}
void garden_render_alpha_support(int supported)
{
    alpha_supported=alpha_enabled=supported!=0;
    if (!alpha_supported) return;
    for (unsigned y=0;y<PANEL_HEIGHT;y++) {
        volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(PANEL_SOURCE+y*PANEL_WIDTH*2U);
        for (unsigned x=0;x<PANEL_WIDTH;x++) {
            out[x]=(x==0U || y==0U || x==PANEL_WIDTH-1U || y==PANEL_HEIGHT-1U) ? 0x7DFFU : 0x2A9FU;
        }
    }
    asm volatile("fence rw,rw" ::: "memory");
    data_cache_invalidate_all();
}
const GardenRenderStats *garden_render_stats(void) { return &stats; }

static int alpha_panel(Rect *painted)
{
    /* A small window over the Boss/bullet area makes the background visible. */
    const int x=GARDEN_X0+272, y=GARDEN_Y0+96;
    uint32_t dst=display_back_addr()+(uint32_t)y*DISPLAY_STRIDE_BYTES+(uint32_t)x*2U;
    if (display_check_render_target(dst)!=GPU_OK) { stats.bad_targets++; return 0; }
    if (use_cpu) {
        for (unsigned row=0;row<PANEL_HEIGHT;row++) {
            const volatile uint16_t *in=(const volatile uint16_t *)(uintptr_t)(PANEL_SOURCE+row*PANEL_WIDTH*2U);
            volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(dst+row*DISPLAY_STRIDE_BYTES);
            for (unsigned col=0;col<PANEL_WIDTH;col++) out[col]=gpu_alpha_rgb565(in[col],out[col],panel_alpha);
        }
        stats.commands++; stats.alpha_commands++;
    } else {
        for (unsigned row=0;row<PANEL_HEIGHT;row+=16U) {
            record_result(gpu_alpha_bitblt(PANEL_SOURCE+row*PANEL_WIDTH*2U,dst+row*DISPLAY_STRIDE_BYTES,PANEL_WIDTH,16U,PANEL_WIDTH*2U,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR,panel_alpha));
            stats.commands++; stats.alpha_commands++;
        }
    }
    painted->x=(int16_t)x; painted->y=(int16_t)y;
    painted->w=PANEL_WIDTH; painted->h=PANEL_HEIGHT;
    return 1;
}

int garden_render_frame(const GardenDraw *draws, unsigned count)
{
    unsigned sel=display_back_sel();
    unsigned before=stats.errors+stats.bad_targets+stats.atlas_bounds;
    unsigned n=0;
    if (count>GARDEN_DRAW_MAX) { stats.errors++; count=GARDEN_DRAW_MAX; }
    if (!initialized[sel]) {
        int saved_cpu=use_cpu; use_cpu=0;
        garden_render_box(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,0x0000);
        garden_render_box(GARDEN_X0-4,GARDEN_Y0-4,GARDEN_WIDTH+8,4,0x3A4B);
        garden_render_box(GARDEN_X0-4,GARDEN_Y0+GARDEN_HEIGHT,GARDEN_WIDTH+8,4,0x3A4B);
        garden_render_box(GARDEN_X0-4,GARDEN_Y0,4,GARDEN_HEIGHT,0x3A4B);
        garden_render_box(GARDEN_X0+GARDEN_WIDTH,GARDEN_Y0,4,GARDEN_HEIGHT,0x3A4B);
        garden_render_text(520,64,"GARDEN OF COLOURED LIGHTS");
        garden_render_text(72,136,"TI60 / RISC-V + FPGA");
        garden_render_text(1000,136,"HOW TO PLAY");
        garden_render_text(1000,176,"W A S D  MOVE");
        garden_render_text(1000,200,"F  SLOW MOVE");
        garden_render_text(1000,224,"AUTO FIRE");
        garden_render_text(1000,248,"P  PAUSE");
        garden_render_text(1000,272,"R  RESTART");
        garden_render_text(1000,296,"T  AUTO DEMO");
        garden_render_text(1000,320,"C  CPU / GPU");
        garden_render_text(1000,344,"B  SAME SCENE BENCH");
        garden_render_text(1000,368,"V  ALPHA / N  LEVEL");
        garden_render_text(72,552,"COLOR KEY RGB565");
        garden_render_text(472,640,"CROPPED PORT / STAGE 1");
        use_cpu=saved_cpu;
        initialized[sel]=1;
    }
    /* Erase the previous contents of THIS physical back buffer first.
     * Then redraw every current object, including ones intersecting an erase. */
    for (unsigned i=0;i<history_count[sel];i++) {
        const Rect *r=&history[sel][i];
        garden_render_box(r->x,r->y,r->w,r->h,0x0000);
    }
    stats.alpha_sprites=0;
    for (unsigned i=0;i<count;i++) {
        if (sprite(draws[i].asset,GARDEN_X0+draws[i].x,GARDEN_Y0+draws[i].y,1,draws[i].alpha,&history[sel][n])) {
            n++;
            if (alpha_enabled && draws[i].alpha<32U) stats.alpha_sprites++;
        }
    }
    if (alpha_enabled) {
        if (n>=GARDEN_DRAW_MAX) stats.errors++;
        else {
            if (alpha_panel(&history[sel][n])) { n++; stats.alpha_sprites++; }
        }
    }
    history_count[sel]=n;
    stats.sprites=n;
    if (use_cpu) { asm volatile("fence rw,rw" ::: "memory"); data_cache_invalidate_all(); }
    return before==stats.errors+stats.bad_targets+stats.atlas_bounds ? GPU_OK : GPU_EHW;
}

int garden_render_key_probe(void)
{
    const garden_sprite_t *s=garden_asset(GARDEN_ASSET_PLAYER);
    unsigned transparent=0,opaque=0,wrong=0;
    uint32_t src=ASSET_BASE_ADDR+s->offset;
    uint32_t dst=display_back_addr()+20U*DISPLAY_STRIDE_BYTES+20U*2U;
    garden_render_box(20,20,s->width,s->height,0x07E0);
    for (unsigned row=0;row<s->height;row+=16U) {
        unsigned band=s->height-row; if (band>16U) band=16U;
        int rc=gpu_bitblt(src+row*s->stride,dst+row*DISPLAY_STRIDE_BYTES,s->width,band,s->stride,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR);
        if (rc!=GPU_OK) return rc;
    }
    data_cache_invalidate_all();
    for (unsigned y=0;y<s->height;y++) for (unsigned x=0;x<s->width;x++) {
        uint16_t in=*(volatile uint16_t *)(uintptr_t)(src+y*s->stride+x*2U);
        uint16_t out=*(volatile uint16_t *)(uintptr_t)(dst+y*DISPLAY_STRIDE_BYTES+x*2U);
        uint16_t expected=in;
        if (in==GARDEN_KEY_COLOR) { expected=0x07E0; transparent++; } else opaque++;
        if (out!=expected) wrong++;
    }
    garden_render_box(20,20,s->width,s->height,0);
    return transparent && opaque && !wrong ? GPU_OK : GPU_EHW;
}
