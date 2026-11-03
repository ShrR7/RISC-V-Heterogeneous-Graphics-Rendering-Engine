/* SPDX-License-Identifier: GPL-3.0-or-later
 * Garden of Coloured Lights: Ti60 demonstration renderer.
 * Dirty rectangles are stored separately for the two physical framebuffers.
 */
#include "garden_render.h"
#include "garden_assets.h"
#include "garden_ui.h"
#include "g9_memory.h"
#include "g9_route.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"
#include "gpu_display.h"
#include "vexriscv.h"
#include <string.h>

typedef G9MemoryRect Rect;
#define history G9_HISTORY
static unsigned history_count[2];
static unsigned initialized[2];
static uint16_t field_color,painted_color[2];
static int use_cpu;
static int alpha_supported, alpha_enabled;
static unsigned panel_alpha;
static unsigned panel_visible;
/* Separate from the atlas and the last 2 KiB used by gpu_alpha_verify. */
#define PANEL_SOURCE (ASSET_BASE_ADDR + ASSET_SIZE_BYTES - 8192U)
#define PANEL_WIDTH 96U
#define PANEL_HEIGHT 32U
static GardenRenderStats stats;
/* The list is DDR scratch, outside the atlas, panel and verification probe.
 * It is reused only after the synchronous GPU operation has completed.
 * Capacity 256*8 fits within one 4KiB page; both layouts have full readable
 * 16-byte final beats. No CPU RAM/stack-sized command array is allocated. */
#define RENDER_BATCH_LIST_ADDR 0x0067D000U
#define RENDER_BATCH_CAPACITY 256U
#define RENDER_FILL_BATCH_LIMIT 256U
#define RENDER_BLIT_BATCH_LIMIT 256U
/* RTL13 bounds each Fill-list read to one beat, releasing DDR ownership. */
_Static_assert(RENDER_FILL_BATCH_LIMIT<=RENDER_BATCH_CAPACITY,"Fill queue capacity");
_Static_assert(RENDER_BLIT_BATCH_LIMIT<=RENDER_BATCH_CAPACITY,"Blit queue capacity");
_Static_assert(RENDER_BATCH_LIST_ADDR>=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES,
               "batch list overlaps atlas");
_Static_assert(RENDER_BATCH_LIST_ADDR+RENDER_BATCH_CAPACITY*8U<=PANEL_SOURCE,
               "batch list overlaps panel/probe");
typedef struct {
    uint32_t src, width, height, src_stride, first_dst;
    uint16_t count, color, key;
    uint8_t kind, alpha;
} RenderBatch;
static RenderBatch batch;
static int use_batch, batch_scope, frame_fault;
static void __attribute__((noinline)) batch_flush_nonempty(void);
/* A tiny shared guard keeps empty CPU/single barriers out of the large
 * list-submit stack frame without repeating that guard at every call site.
 * A real queue still uses the same synchronous error-safe drain. */
static void __attribute__((noinline)) batch_flush(void)
{
    if(batch.count) batch_flush_nonempty();
}
static void gpu_fill_submit(uint32_t dst,unsigned w,unsigned h,uint16_t color);
static void gpu_blit_submit(uint32_t src,uint32_t dst,unsigned w,unsigned h,
                            unsigned src_stride,unsigned alpha);

/* The host pixel contract compiles the production CPU and GPU renderer paths.
 * Only the RISC-V fence/cache instruction is replaced by a host callback. */
static void render_fence(void)
{
#ifdef GARDEN_RENDER_HOST_TEST
    extern void garden_render_host_fence(void);
    garden_render_host_fence();
#else
    asm volatile("fence rw,rw" ::: "memory");
    data_cache_invalidate_all();
#endif
}

static void record_result(int rc)
{
    if (rc != GPU_OK) stats.errors++;
}

/* Normal successful submissions correspond one-for-one with CTRL starts.
 * An error can be rejected before start or occur after partial execution;
 * any frame with an error is excluded from performance comparisons. */
static void submission_result(int rc,unsigned alpha)
{
    record_result(rc);
    if(rc!=GPU_OK) frame_fault=1;
    if(rc!=GPU_EINVAL && rc!=GPU_EOVERLAP && rc!=GPU_ESTATE) {
        stats.commands++;
        if(alpha<32U) stats.alpha_commands++;
    }
}

static void __attribute__((noinline)) batch_flush_nonempty(void)
{
    unsigned n=batch.count,kind=batch.kind;
    if(!n) return;
    /* Consume before the call. A partial/failed list must never be replayed
     * as singles: Alpha would blend pixels a second time. Dirty history
     * still retains all attempted rectangles for the next buffer redraw. */
    batch.count=0;
    if(frame_fault) return;
    int rc;
    if(kind==1U) {
        const GpuFillEntry *list=(const GpuFillEntry *)(uintptr_t)RENDER_BATCH_LIST_ADDR;
        if(n==1U) rc=gpu_fill(batch.first_dst,batch.width,batch.height,DISPLAY_STRIDE_BYTES,batch.color);
        else rc=gpu_fill_batch(list,n,DISPLAY_STRIDE_BYTES,batch.color);
    } else {
        const GpuBlitEntry *list=(const GpuBlitEntry *)(uintptr_t)RENDER_BATCH_LIST_ADDR;
        if(n>1U) rc=gpu_bitblt_batch(list,n,batch.src,batch.width,batch.height,batch.src_stride,DISPLAY_STRIDE_BYTES,batch.key,batch.alpha);
        else if(batch.alpha<32U) rc=gpu_alpha_bitblt(batch.src,batch.first_dst,batch.width,batch.height,batch.src_stride,DISPLAY_STRIDE_BYTES,batch.key,batch.alpha);
        else rc=gpu_bitblt(batch.src,batch.first_dst,batch.width,batch.height,batch.src_stride,DISPLAY_STRIDE_BYTES,batch.key);
    }
    submission_result(rc,kind==2U ? batch.alpha:32U);
    if(n>1U && rc!=GPU_EINVAL && rc!=GPU_EOVERLAP && rc!=GPU_ESTATE) {
        stats.batch_commands++;
        stats.batch_instances+=n;
        if(kind==1U) stats.batch_fill_commands++;
        else stats.batch_blit_commands++;
    }
}

static void gpu_fill_submit(uint32_t dst,unsigned w,unsigned h,uint16_t color)
{
    if(frame_fault) return;
    if(!use_batch || !batch_scope) {
        batch_flush();submission_result(gpu_fill(dst,w,h,DISPLAY_STRIDE_BYTES,color),32U);return;
    }
    if(batch.count && (batch.kind!=1U || batch.color!=color || batch.count==RENDER_FILL_BATCH_LIMIT)) batch_flush();
    if(frame_fault) return;
    batch.kind=1U;batch.color=color;
    if(!batch.count) {
        /* The common singleton never touches DDR. Fill reuses the common
         * geometry slots to retain its first variable-sized rectangle. */
        batch.first_dst=dst;batch.width=w;batch.height=h;
    } else {
        volatile GpuFillEntry *list=(volatile GpuFillEntry *)(uintptr_t)RENDER_BATCH_LIST_ADDR;
        if(batch.count==1U) {
            list[0].dst=batch.first_dst;list[0].width=(uint16_t)batch.width;list[0].height=(uint16_t)batch.height;
        }
        list[batch.count].dst=dst;list[batch.count].width=(uint16_t)w;list[batch.count].height=(uint16_t)h;
    }
    batch.count++;
}

static void gpu_blit_submit(uint32_t src,uint32_t dst,unsigned w,unsigned h,
                            unsigned src_stride,unsigned alpha)
{
    if(frame_fault) return;
    if(!use_batch || !batch_scope) {
        batch_flush();
        int rc=alpha<32U ? gpu_alpha_bitblt(src,dst,w,h,src_stride,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR,alpha):
                           gpu_bitblt(src,dst,w,h,src_stride,DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR);
        submission_result(rc,alpha);return;
    }
    if(batch.count && (batch.kind!=2U || batch.src!=src || batch.width!=w || batch.height!=h ||
       batch.src_stride!=src_stride || batch.key!=GARDEN_KEY_COLOR || batch.alpha!=alpha ||
       batch.count==RENDER_BLIT_BATCH_LIMIT)) batch_flush();
    if(frame_fault) return;
    batch.kind=2U;batch.src=src;batch.width=w;batch.height=h;
    batch.src_stride=src_stride;batch.key=GARDEN_KEY_COLOR;batch.alpha=(uint8_t)alpha;
    if(!batch.count) batch.first_dst=dst;
    else {
        volatile GpuBlitEntry *list=(volatile GpuBlitEntry *)(uintptr_t)RENDER_BATCH_LIST_ADDR;
        if(batch.count==1U) list[0].dst=batch.first_dst;
        list[batch.count].dst=dst;
    }
    batch.count++;
}

/* Keep large UI clears at the original 16 rows. Narrow rectangles can use
 * more of RTL11's multi-line command path, with both a 64-row and 4096-pixel
 * bound. Width is positive after clipping at every caller. This is a bounded
 * batching policy, not removal of engine/driver watchdogs. */
static unsigned render_band_rows(unsigned width)
{
    if (width > 128U) return 16U;
    unsigned rows = 4096U / width;
    return rows < 64U ? rows : 64U;
}

static unsigned erase_bands(Rect r)
{
    unsigned rows=render_band_rows(r.w);
    return (r.h+rows-1U)/rows;
}

/* History is already clipped to the logical field; a bounding union remains
 * there. More background may be erased, then the same stars and all current
 * objects are redrawn. Require nonincreasing pixel area AND fewer Fill bands.
 * The compact prefix is reused in place. Each comparison advances j or drops
 * one prefix entry; no rescans: at most count*(count-1)/2 comparisons,240max.
 */
static unsigned merge_erases(Rect *rects,unsigned count)
{
    unsigned kept=0U;
    for(unsigned i=0U;i<count;i++) {
        Rect next=rects[i];unsigned j=0U;
        while(j<kept) {
            Rect old=rects[j];
            int oright=old.x+old.w,nright=next.x+next.w;
            int obottom=old.y+old.h,nbottom=next.y+next.h;
            if(oright<next.x || nright<old.x || obottom<next.y || nbottom<old.y) { j++;continue; }
            int left=old.x<next.x ? old.x:next.x;
            int top=old.y<next.y ? old.y:next.y;
            int right=oright>nright ? oright:nright;
            int bottom=obottom>nbottom ? obottom:nbottom;
            Rect box={(int16_t)left,(int16_t)top,(uint16_t)(right-left),(uint16_t)(bottom-top)};
            if((uint32_t)box.w*box.h<=(uint32_t)old.w*old.h+(uint32_t)next.w*next.h &&
               erase_bands(box)<erase_bands(old)+erase_bands(next)) {
                next=box;rects[j]=rects[--kept];
            } else j++;
        }
        rects[kept++]=next;
    }
    return kept;
}

void garden_render_box(int x, int y, int w, int h, uint16_t color)
{
    uint32_t dst;
    if(frame_fault) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)DISPLAY_WIDTH) w = (int)DISPLAY_WIDTH - x;
    if (y + h > (int)DISPLAY_HEIGHT) h = (int)DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    dst = display_back_addr() + (uint32_t)y * DISPLAY_STRIDE_BYTES + (uint32_t)x * 2U;
    if (display_check_render_target(dst) != GPU_OK) { stats.bad_targets++; return; }
    if (use_cpu) {
        batch_flush();
        for (int row = 0; row < h; row++) {
            volatile uint16_t *out = (volatile uint16_t *)(uintptr_t)(dst + (uint32_t)row * DISPLAY_STRIDE_BYTES);
            for (int col = 0; col < w; col++) out[col] = color;
        }
        stats.commands++;
    } else {
        unsigned band_rows = render_band_rows((unsigned)w);
        for (int row = 0; row < h; row += (int)band_rows) {
            unsigned band = (unsigned)(h-row);
            if (band > band_rows) band = band_rows;
            gpu_fill_submit(dst+(uint32_t)row*DISPLAY_STRIDE_BYTES,(unsigned)w,band,color);
        }
    }
}

static int sprite_region(unsigned id, int x, int y, int in_field, unsigned alpha,
                         unsigned first_col, unsigned cols, unsigned first_row,
                         unsigned rows, int shift, Rect *painted)
{
    const garden_sprite_t *s = garden_asset(id);
    if(frame_fault) return 0;
    int sx = (int)first_col, sy = (int)first_row, w, h;
    int xmin = in_field ? GARDEN_X0 : 0;
    int ymin = in_field ? GARDEN_Y0 : 0;
    int xmax = in_field ? GARDEN_X0+GARDEN_WIDTH : (int)DISPLAY_WIDTH;
    int ymax = in_field ? GARDEN_Y0+GARDEN_HEIGHT : (int)DISPLAY_HEIGHT;
    uint32_t src, dst;
    uint64_t source_end;
    if (alpha > 32U) { stats.errors++; return 0; }
    if (!alpha_enabled) alpha=32U;
    if (!s || !s->width || !s->height || garden_sprite_stride(s) < s->width*2U) { stats.atlas_bounds++; return 0; }
    if(first_row>=s->height || !rows || first_col>=s->width || !cols) { stats.atlas_bounds++; return 0; }
    x -= s->anchor_x; x += shift+(int)first_col;
    y -= s->anchor_y; y += (int)first_row;
    w = (int)(s->width-first_col); h = (int)(s->height-first_row);
    if(cols<(unsigned)w) w=(int)cols;
    if(rows<(unsigned)h) h=(int)rows;
    if (x < xmin) { int clipped=xmin-x; sx+=clipped; w-=clipped; x=xmin; }
    if (y < ymin) { int clipped=ymin-y; sy+=clipped; h-=clipped; y=ymin; }
    if (x+w > xmax) w=xmax-x;
    if (y+h > ymax) h=ymax-y;
    if (w <= 0 || h <= 0) return 0;
    src = ASSET_BASE_ADDR + s->offset + (uint32_t)sy*garden_sprite_stride(s) + (uint32_t)sx*2U;
    source_end = (uint64_t)src + (uint64_t)(h-1)*garden_sprite_stride(s) + (unsigned)w*2U;
    if (source_end > (uint64_t)ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES || (src & 1U)) { stats.atlas_bounds++; return 0; }
    dst = display_back_addr() + (uint32_t)y*DISPLAY_STRIDE_BYTES + (uint32_t)x*2U;
    if (display_check_render_target(dst) != GPU_OK) { stats.bad_targets++; return 0; }
    /* A zero-opacity source cannot change a destination pixel. Keep all
     * parameter/source/target checks above, but omit the no-op command and
     * invisible dirty rectangle. Alpha-off has already mapped it to32. */
    if (!alpha) return 0;
    if (use_cpu) {
        batch_flush();
        for (int row=0; row<h; row++) {
            const volatile uint16_t *in = (const volatile uint16_t *)(uintptr_t)(src+(uint32_t)row*garden_sprite_stride(s));
            volatile uint16_t *out = (volatile uint16_t *)(uintptr_t)(dst+(uint32_t)row*DISPLAY_STRIDE_BYTES);
            for (int col=0; col<w; col++) {
                uint16_t v=in[col];
                if (v!=GARDEN_KEY_COLOR) out[col]=alpha==32U ? v : gpu_alpha_rgb565(v,out[col],alpha);
            }
        }
        stats.commands++;
        if (alpha<32U) stats.alpha_commands++;
    } else {
        unsigned band_rows=render_band_rows((unsigned)w);
        for (int row=0; row<h; row+=(int)band_rows) {
            unsigned band=(unsigned)(h-row); if (band>band_rows) band=band_rows;
            gpu_blit_submit(src+(uint32_t)row*garden_sprite_stride(s),dst+(uint32_t)row*DISPLAY_STRIDE_BYTES,
                            (unsigned)w,band,garden_sprite_stride(s),alpha);
        }
    }
    if (painted) { painted->x=(int16_t)x; painted->y=(int16_t)y; painted->w=(uint16_t)w; painted->h=(uint16_t)h; }
    return 1;
}

static int sprite_part(unsigned id,int x,int y,int in_field,unsigned alpha,
                       unsigned first_row,unsigned rows,int shift,Rect *painted)
{ return sprite_region(id,x,y,in_field,alpha,0U,0xFFFFU,first_row,rows,shift,painted); }

typedef struct { uint8_t x,y,w,h; } SpriteInk;
/* All enemy projectile directions are now tightly baked from the nine user
 * PNGs. No historical DART crop may cut their new nuclei or silhouettes. */
static const SpriteInk explosion_ink[4]={
    {5,5,21,21},{2,2,29,29},{3,3,31,31},{3,4,33,32}
};
static int sprite(unsigned id,int x,int y,int in_field,unsigned alpha,Rect *painted)
{
    const SpriteInk *ink=0;
    if(id>=GARDEN_ASSET_EXPLOSION0 && id<=GARDEN_ASSET_EXPLOSION3)
        ink=&explosion_ink[id-GARDEN_ASSET_EXPLOSION0];
    if(ink) return sprite_region(id,x,y,in_field,alpha,ink->x,ink->w,ink->y,ink->h,0,painted);
    return sprite_part(id,x,y,in_field,alpha,0U,0xFFFFU,0,painted);
}

static int bank_sprite(unsigned value,int x,int y,unsigned alpha,Rect *painted)
{
    unsigned pose=(value>>2)&7U,frame=value&3U;
    unsigned id=frame ? GARDEN_ASSET_PLAYER_FRAME1+frame-1U:GARDEN_ASSET_PLAYER;
    const garden_sprite_t *s=garden_asset(id);
    int bank=(int)pose-2,any=0;
    if((value&~31U) || pose>4U || frame>2U || alpha>32U) { stats.errors++;return 0; }
    if(!bank) return sprite(id,x,y,1,alpha,painted);
    if(!s || s->height<6U) { stats.atlas_bounds++;return 0; }
    /* Six disjoint horizontal bands shear the original frame about its true
     * collision anchor: lean toward motion at the nose, away at the tail.
     * The 34x29 source needs six <=5-row BitBlt commands, without an atlas
     * allocation, scratch write, scale unit, or change to collision geometry.
     * All painted bands are erased together as one composite history rect. */
    for(unsigned band=0;band<6U;band++) {
        unsigned first=band*s->height/6U,end=(band+1U)*s->height/6U;
        int middle=(int)((first+end-1U)/2U);
        int shift=bank*((int)s->anchor_y-middle)/6;
        Rect part;
        if(sprite_part(id,x,y,1,alpha,first,end-first,shift,&part)) {
            if(!any) { *painted=part;any=1; }
            else {
                int left=painted->x<part.x ? painted->x:part.x;
                int top=painted->y<part.y ? painted->y:part.y;
                int right=painted->x+painted->w>part.x+part.w ? painted->x+painted->w:part.x+part.w;
                int bottom=painted->y+painted->h>part.y+part.h ? painted->y+painted->h:part.y+part.h;
                *painted=(Rect){(int16_t)left,(int16_t)top,(uint16_t)(right-left),(uint16_t)(bottom-top)};
            }
        }
    }
    return any;
}

unsigned garden_render_score_width(unsigned value)
{
    unsigned digits=0U;
    if (!value || value>GARDEN_DRAW_SCORE_MAX) return 0U;
    do { digits++; value/=10U; } while (value);
    return GARDEN_DRAW_SCORE_PLUS_ADVANCE+digits*GARDEN_DRAW_SCORE_DIGIT_ADVANCE;
}

static void score_box(int x,int y,int w,int h,uint16_t color)
{
    if (x<GARDEN_X0) { w-=GARDEN_X0-x; x=GARDEN_X0; }
    if (y<GARDEN_Y0) { h-=GARDEN_Y0-y; y=GARDEN_Y0; }
    if (x+w>GARDEN_X0+GARDEN_WIDTH) w=GARDEN_X0+GARDEN_WIDTH-x;
    if (y+h>GARDEN_Y0+GARDEN_HEIGHT) h=GARDEN_Y0+GARDEN_HEIGHT-y;
    if (w>0 && h>0) garden_render_box(x,y,w,h,color);
}

typedef struct { uint8_t x,y,w,h; } DigitCrop;
/* Generated once from frozen atlas CRC32 2A64AD91, white/cyan large glyphs.
 * Only fully Color-Key outer margins are omitted. Font spacing, anchoring,
 * opacity and raster size stay exact; history is the full ink union.
 * Host verification scans all 20 actual glyphs and compares full 16x16
 * independent oracles; changing the font atlas requires regenerating these.
 * 80 bytes of immutable metadata; no runtime atlas scan or extra scratch. */
static const DigitCrop damage_digit_crop[2][10]={
    {{2,0,10,14},{4,0,6,14},{2,0,10,14},{2,0,10,14},{2,0,10,14},
     {2,0,10,14},{2,0,10,14},{2,0,10,14},{2,0,10,14},{2,0,10,14}},
    {{2,0,10,14},{4,0,6,14},{2,0,10,14},{2,0,10,14},{2,0,10,14},
     {2,0,10,14},{2,0,10,14},{2,0,10,14},{2,0,10,14},{2,0,10,14}}
};

static int score_sprite(unsigned value,int x,int y,unsigned alpha,Rect *painted)
{
    char reverse[5]; unsigned digits=0U;
    unsigned width=garden_render_score_width(value);
    int left=x,top=y,right=x+(int)width,bottom=y+(int)GARDEN_DRAW_SCORE_HEIGHT;
    int ink_left=x+2,ink_top=y+7,ink_right=x+10,ink_bottom=y+9;
    unsigned font=value==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE;
    uint16_t color=value==1U ? 0xFFFFU:GARDEN_UI_CYAN;
    if (!width || alpha>32U) { stats.errors++; return 0; }
    if (!alpha_enabled) alpha=32U;
    if (!alpha) return 0;
    if (left<GARDEN_X0) left=GARDEN_X0;
    if (top<GARDEN_Y0) top=GARDEN_Y0;
    if (right>GARDEN_X0+GARDEN_WIDTH) right=GARDEN_X0+GARDEN_WIDTH;
    if (bottom>GARDEN_Y0+GARDEN_HEIGHT) bottom=GARDEN_Y0+GARDEN_HEIGHT;
    if (left>=right || top>=bottom) return 0;
    /* Minus is a code-native fill. Its fade scales RGB565 against black;
     * existing font digits use the real destination-reading Alpha BitBlt.
     * No procedural mask or scratch source can collide with the full atlas. */
    if (alpha<32U) color=gpu_alpha_rgb565(color,0U,alpha);
    score_box(x+2,y+7,8,2,color);
    do { reverse[digits++]=(char)('0'+value%10U); value/=10U; } while (value);
    x+=(int)GARDEN_DRAW_SCORE_PLUS_ADVANCE;
    while (digits) {
        char digit=reverse[--digits];
        int glyph=garden_glyph(digit);
        const DigitCrop *crop=&damage_digit_crop[font==GARDEN_ASSET_FONT_LARGE_CYAN_BASE][(unsigned)(digit-'0')];
        if (glyph<0) { stats.atlas_bounds++; return 0; }
        sprite_region(font+(unsigned)glyph-GARDEN_ASSET_FONT_WHITE_BASE,x,y,1,alpha,
                      crop->x,crop->w,crop->y,crop->h,0,0);
        if(x+crop->x<ink_left) ink_left=x+crop->x;
        if(y+crop->y<ink_top) ink_top=y+crop->y;
        if(x+crop->x+crop->w>ink_right) ink_right=x+crop->x+crop->w;
        if(y+crop->y+crop->h>ink_bottom) ink_bottom=y+crop->y+crop->h;
        x+=(int)GARDEN_DRAW_SCORE_DIGIT_ADVANCE;
    }
    /* One common history entry, bounded to the minus/digit ink union.
     * Transparent outer margins never touch the destination, so clearing
     * them would only create fill traffic. Public layout width/advance and
     * the complete rendered pixels remain unchanged. Retain all attempted
     * ink bands even after a GPU failure so the next frame erases partials. */
    if(ink_left<GARDEN_X0) ink_left=GARDEN_X0;
    if(ink_top<GARDEN_Y0) ink_top=GARDEN_Y0;
    if(ink_right>GARDEN_X0+GARDEN_WIDTH) ink_right=GARDEN_X0+GARDEN_WIDTH;
    if(ink_bottom>GARDEN_Y0+GARDEN_HEIGHT) ink_bottom=GARDEN_Y0+GARDEN_HEIGHT;
    if(ink_left>=ink_right || ink_top>=ink_bottom) return 0;
    painted->x=(int16_t)ink_left; painted->y=(int16_t)ink_top;
    painted->w=(uint16_t)(ink_right-ink_left); painted->h=(uint16_t)(ink_bottom-ink_top);
    return 1;
}

static int health_bar(unsigned value,int x,int y,unsigned alpha,Rect *painted)
{
    unsigned boss=(value&GARDEN_DRAW_BAR_BOSS)!=0U;
    unsigned fill=value&GARDEN_DRAW_BAR_FILL_MASK;
    unsigned width=boss ? 128U:40U,inner=width-2U;
    int left=x,top=y,right=x+(int)width,bottom=y+8;
    uint16_t color=boss ? GARDEN_UI_CYAN:0x5FEAU;
    if((value&~0x01FFU) || fill>inner || alpha>32U) { stats.errors++;return 0; }
    if(!alpha_enabled) alpha=32U;
    if(!alpha) return 0;
    if(left<GARDEN_X0) left=GARDEN_X0;
    if(top<GARDEN_Y0) top=GARDEN_Y0;
    if(right>GARDEN_X0+GARDEN_WIDTH) right=GARDEN_X0+GARDEN_WIDTH;
    if(bottom>GARDEN_Y0+GARDEN_HEIGHT) bottom=GARDEN_Y0+GARDEN_HEIGHT;
    if(left>=right || top>=bottom) return 0;
    if(fill*3U<inner) color=0xFD20U;
    /* Bars are normally opaque. For fallback Alpha display, code-native
     * fills scale against black like the existing procedural damage sign.
     * No unclaimed destination-aware Alpha is used for these fills. */
    uint16_t border=0xAD75U,empty=0x1084U;
    if(alpha<32U) {
        color=gpu_alpha_rgb565(color,0U,alpha);
        border=gpu_alpha_rgb565(border,0U,alpha);
        empty=gpu_alpha_rgb565(empty,0U,alpha);
    }
    score_box(x,y,(int)width,8,border);
    /* Only the unfilled right segment needs the empty colour. Repainting
     * the filled segment first was hidden traffic; a full bar needs no
     * empty command at all. Border and all final RGB565 pixels stay exact. */
    if(fill<inner) score_box(x+1+(int)fill,y+1,(int)(inner-fill),6,empty);
    if(fill) score_box(x+1,y+1,(int)fill,6,color);
    *painted=(Rect){(int16_t)left,(int16_t)top,(uint16_t)(right-left),(uint16_t)(bottom-top)};
    return 1;
}

void garden_render_text_style(int x, int y, const char *text, unsigned style)
{
    batch_flush();
    int start=x;
    int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16 : 8;
    unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE :
                  style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE :
                  style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE : GARDEN_ASSET_FONT_WHITE_BASE;
    while (*text) {
        int id=garden_glyph(*text);
        if (*text=='\n') { x=start; y+=advance+8; }
        else { if (id>=0 && *text!=' ') sprite(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x,y,0,32U,0); x+=advance; }
        text++;
    }
    batch_flush();
}
void garden_render_text(int x, int y, const char *text)
{ garden_render_text_style(x,y,text,GARDEN_TEXT_SMALL_WHITE); }

void garden_render_init(void)
{
    batch_flush();
    /* A timed-out engine may still be reading the DDR list. In that case
     * initialization cannot release/reuse its storage or touch either FB. */
    if(frame_fault && gpu_busy()) return;
    frame_fault=0;
    batch_scope=0;use_batch=0;
    memset(&batch,0,sizeof(batch));
    garden_ui_hud_reset();
    memset(history_count,0,sizeof(history_count));
    memset(initialized,0,sizeof(initialized));
    field_color=painted_color[0]=painted_color[1]=0U;
    memset(&stats,0,sizeof(stats));
    use_cpu=0;
    alpha_supported=alpha_enabled=0;
    panel_alpha=16U;
    panel_visible=0U;
}
int garden_render_surface_reset(void)
{
    batch_flush();
    if(frame_fault || gpu_wait_idle(3000000U)!=GPU_OK) return GPU_EHW;
    batch_scope=0;
    history_count[0]=history_count[1]=0U;
    initialized[0]=initialized[1]=0U;
    field_color=painted_color[0]=painted_color[1]=0U;
    garden_ui_hud_reset();
    return GPU_OK;
}
void garden_render_field_color(uint16_t color) { field_color=color; }
void garden_render_mode(int cpu) { batch_flush();use_cpu=cpu!=0; }
void garden_render_batch_mode(int enabled) { batch_flush();use_batch=enabled!=0; }
int garden_render_is_batch(void) { return use_batch; }
int garden_render_is_cpu(void) { return use_cpu; }
int garden_render_alpha_supported(void) { return alpha_supported; }
int garden_render_alpha_enabled(void) { return alpha_enabled; }
unsigned garden_render_alpha_level(void) { return panel_alpha; }
void garden_render_panel_visible(unsigned visible) { panel_visible=visible!=0U; }
int garden_render_panel_is_visible(void) { return panel_visible!=0U; }
int garden_render_alpha_toggle(void)
{
    batch_flush();
    if (!alpha_supported) return GPU_ESTATE;
    alpha_enabled=!alpha_enabled;
    return GPU_OK;
}
void garden_render_alpha_cycle(void)
{
    batch_flush();
    if (alpha_supported) panel_alpha=panel_alpha==24U ? 8U : panel_alpha+8U;
}
void garden_render_alpha_support(int supported)
{
    batch_flush();
    if(frame_fault) return;
    alpha_supported=alpha_enabled=supported!=0;
    if (!alpha_supported) return;
    for (unsigned y=0;y<PANEL_HEIGHT;y++) {
        volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(PANEL_SOURCE+y*PANEL_WIDTH*2U);
        for (unsigned x=0;x<PANEL_WIDTH;x++) {
            out[x]=garden_ui_alpha_pixel(x,y);
        }
    }
    render_fence();
}
const GardenRenderStats *garden_render_stats(void) { return &stats; }

static int alpha_panel(Rect *painted)
{
    /* A small window over the Boss/bullet area makes the background visible. */
    const int x=GARDEN_X0+272, y=GARDEN_Y0+96;
    uint32_t dst=display_back_addr()+(uint32_t)y*DISPLAY_STRIDE_BYTES+(uint32_t)x*2U;
    if(frame_fault) return 0;
    if (display_check_render_target(dst)!=GPU_OK) { stats.bad_targets++; return 0; }
    if (use_cpu) {
        batch_flush();
        for (unsigned row=0;row<PANEL_HEIGHT;row++) {
            const volatile uint16_t *in=(const volatile uint16_t *)(uintptr_t)(PANEL_SOURCE+row*PANEL_WIDTH*2U);
            volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(dst+row*DISPLAY_STRIDE_BYTES);
            for (unsigned col=0;col<PANEL_WIDTH;col++) out[col]=gpu_alpha_rgb565(in[col],out[col],panel_alpha);
        }
        stats.commands++; stats.alpha_commands++;
    } else {
        for (unsigned row=0;row<PANEL_HEIGHT;row+=16U) {
            gpu_blit_submit(PANEL_SOURCE+row*PANEL_WIDTH*2U,dst+row*DISPLAY_STRIDE_BYTES,PANEL_WIDTH,16U,PANEL_WIDTH*2U,panel_alpha);
        }
    }
    painted->x=(int16_t)x; painted->y=(int16_t)y;
    painted->w=PANEL_WIDTH; painted->h=PANEL_HEIGHT;
    return 1;
}

static int forming_sprite(unsigned value,int x,int y,unsigned alpha,Rect *painted)
{
    unsigned id=value&GARDEN_DRAW_FORM_ASSET_MASK;
    const garden_sprite_t *s=garden_asset(id);
    unsigned radius=value&GARDEN_DRAW_FORM_GROW ? 4U:1U;
    if(!s || !garden_asset_is_enemy_bullet(id) || s->anchor_x<0 || s->anchor_y<0 ||
       s->anchor_x>=s->width || s->anchor_y>=s->height) { stats.errors++;return 0; }
    unsigned left=(unsigned)s->anchor_x>radius ? (unsigned)s->anchor_x-radius:0U;
    unsigned top=(unsigned)s->anchor_y>radius ? (unsigned)s->anchor_y-radius:0U;
    unsigned right=(unsigned)s->anchor_x+radius+1U,bottom=(unsigned)s->anchor_y+radius+1U;
    if(right>s->width) right=s->width;
    if(bottom>s->height) bottom=s->height;
    return sprite_region(id,x,y,1,alpha,left,right-left,top,bottom-top,0,painted);
}

static int laser_sprite(unsigned value,int x,int y,unsigned alpha,Rect *painted)
{
    unsigned width=value&GARDEN_DRAW_LASER_WIDTH_MASK;
    unsigned active=value&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE);
    unsigned id=GARDEN_ASSET_ORDNANCE_BEAM_BASE+((value&GARDEN_DRAW_LASER_FRAME)!=0U);
    const garden_sprite_t *s=garden_asset(id);
    int any=0;
    if((value&~0x0FFFU) || width!=13U || !s || s->width!=45U ||
       s->height<32U || s->anchor_x!=22 || s->anchor_y!=0) {
        stats.errors++;return 0;
    }
    if(alpha>32U) { stats.errors++;return 0; }
    /* Real45px texture:13px bright core at x[-6,+6],16px dim plasma each side.
     * Preview crops only the centre column. The source repeats vertically;
     * it never stretches the dangerous core or reaches above the muzzle. */
    unsigned col=active ? 0U:22U,cols=active ? 45U:1U;
    unsigned phase=((value&GARDEN_DRAW_LASER_SCROLL_MASK)>>GARDEN_DRAW_LASER_SCROLL_SHIFT)*3U%24U;
    int row=y<GARDEN_Y0 ? GARDEN_Y0:y;
    while(row<GARDEN_Y0+GARDEN_HEIGHT) {
        unsigned source_row=((unsigned)(row-y)+phase)%s->height;
        unsigned rows=(unsigned)(GARDEN_Y0+GARDEN_HEIGHT-row);
        if(rows>s->height-source_row) rows=s->height-source_row;
        Rect part;
        /* sprite_region adds the selected source-row to destination y. Undo
         * that geometric offset here; only its actual source phase scrolls. */
        if(sprite_region(id,x,row-(int)source_row,1,alpha,col,cols,source_row,rows,0,&part)) {
            if(!any) { *painted=part;any=1; }
            else {
                int bottom=part.y+part.h;
                if(bottom>painted->y+painted->h) painted->h=(uint16_t)(bottom-painted->y);
            }
        }
        row+=(int)rows;
    }
    return any;
}

int garden_render_frame(const GardenDraw *draws, unsigned count)
{
    batch_flush();
    if(frame_fault && gpu_busy()) return GPU_EHW;
    frame_fault=0;
    unsigned sel=display_back_sel();
    unsigned before=stats.errors+stats.bad_targets+stats.atlas_bounds;
    unsigned n=0;
    if (sel>=2U) { stats.bad_targets++; return GPU_ESTATE; }
    if (!draws && count) { stats.errors++; return GPU_EINVAL; }
    if (count>GARDEN_DRAW_MAX) { stats.errors++; count=GARDEN_DRAW_MAX; }
    if (!initialized[sel]) {
        int saved_cpu=use_cpu; use_cpu=0;
        garden_ui_static();
        use_cpu=saved_cpu;
        initialized[sel]=1;
    }
    batch_scope=1;
    unsigned field_repaint=field_color!=painted_color[sel];
    if (field_repaint) {
        garden_render_box(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT,field_color);
        if (!field_color) garden_ui_stars(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT);
        /* The full field replacement already erases this buffer's history. */
        history_count[sel]=0U;
        painted_color[sel]=field_color;
    }
    /* Erase the previous contents of THIS physical back buffer first.
     * Then redraw every current object, including ones intersecting an erase. */
    unsigned erase_count=merge_erases(history[sel],history_count[sel]);
    for (unsigned i=0;i<erase_count;i++) {
        const Rect *r=&history[sel][i];
        garden_render_box(r->x,r->y,r->w,r->h,field_color);
        if (!field_color) garden_ui_stars(r->x,r->y,r->w,r->h);
    }
    stats.alpha_sprites=0;
    for (unsigned i=0;i<count;i++) {
        if(g9_route_skip(i))continue;
        unsigned asset=draws[i].asset;
        int x=GARDEN_X0+draws[i].x,y=GARDEN_Y0+draws[i].y;
        int painted;
        if(asset&GARDEN_DRAW_SCORE_BASE)
            painted=score_sprite(asset&GARDEN_DRAW_SCORE_MAX,x,y,draws[i].alpha,&history[sel][n]);
        else if(asset&GARDEN_DRAW_BAR_BASE)
            painted=health_bar(asset&~GARDEN_DRAW_BAR_BASE,x,y,draws[i].alpha,&history[sel][n]);
        else if(asset&GARDEN_DRAW_BANK_BASE)
            painted=bank_sprite(asset&~GARDEN_DRAW_BANK_BASE,x,y,draws[i].alpha,&history[sel][n]);
        else if(asset&GARDEN_DRAW_LASER_BASE)
            painted=laser_sprite(asset&~GARDEN_DRAW_LASER_BASE,x,y,draws[i].alpha,&history[sel][n]);
        else if(asset&GARDEN_DRAW_FORM_BASE)
            painted=forming_sprite(asset&~GARDEN_DRAW_FORM_BASE,x,y,draws[i].alpha,&history[sel][n]);
        else painted=sprite(asset,x,y,1,draws[i].alpha,&history[sel][n]);
        if (painted) {
            n++;
            if (alpha_enabled && draws[i].alpha<32U) stats.alpha_sprites++;
        }
    }
    if (alpha_enabled && panel_visible) {
        if (n>=GARDEN_DRAW_MAX) stats.errors++;
        else {
            if (alpha_panel(&history[sel][n])) { n++; stats.alpha_sprites++; }
        }
    }
    history_count[sel]=n;
    stats.sprites=n;
    batch_flush();batch_scope=0;
    if (use_cpu) render_fence();
    int complete=!frame_fault && before==stats.errors+stats.bad_targets+stats.atlas_bounds;
    /* A failed full-field fill may have painted only part of a pulse. Force
     * a complete static/base redraw after the engine is safely idle, even if
     * the warning already expired while the hardware was being recovered. */
    if (!complete && field_repaint) { initialized[sel]=0U;painted_color[sel]=0U; }
    return complete ? GPU_OK : GPU_EHW;
}

int garden_render_key_probe(void)
{
    batch_flush();
    if(frame_fault) return GPU_EHW;
    const garden_sprite_t *s=garden_asset(GARDEN_ASSET_PLAYER);
    unsigned transparent=0,opaque=0,wrong=0;
    uint32_t src=ASSET_BASE_ADDR+s->offset;
    uint32_t dst=display_back_addr()+20U*DISPLAY_STRIDE_BYTES+20U*2U;
    garden_render_box(20,20,s->width,s->height,0x07E0);
    batch_flush();
    for (unsigned row=0;row<s->height;row+=16U) {
        unsigned band=s->height-row; if (band>16U) band=16U;
        int rc=gpu_bitblt(src+row*garden_sprite_stride(s),dst+row*DISPLAY_STRIDE_BYTES,s->width,band,garden_sprite_stride(s),DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR);
        if (rc!=GPU_OK) return rc;
    }
    data_cache_invalidate_all();
    for (unsigned y=0;y<s->height;y++) for (unsigned x=0;x<s->width;x++) {
        uint16_t in=*(volatile uint16_t *)(uintptr_t)(src+y*garden_sprite_stride(s)+x*2U);
        uint16_t out=*(volatile uint16_t *)(uintptr_t)(dst+y*DISPLAY_STRIDE_BYTES+x*2U);
        uint16_t expected=in;
        if (in==GARDEN_KEY_COLOR) { expected=0x07E0; transparent++; } else opaque++;
        if (out!=expected) wrong++;
    }
    garden_render_box(20,20,s->width,s->height,0);
    return transparent && opaque && !wrong ? GPU_OK : GPU_EHW;
}
