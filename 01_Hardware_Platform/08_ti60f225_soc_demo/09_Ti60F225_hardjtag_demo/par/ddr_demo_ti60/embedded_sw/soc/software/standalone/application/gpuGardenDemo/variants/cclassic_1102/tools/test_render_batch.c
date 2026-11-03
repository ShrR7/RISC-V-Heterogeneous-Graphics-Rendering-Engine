/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent geometry difference and real CPU/GPU renderer pixel contracts.
 * Source reference is the frozen pre-batch precision driver; host only.
 */
#ifdef GPU_DRIVER_GEOMETRY_HOST_TEST
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "gpu_driver.h"
#include "gpu_regs.h"
#define GPU_MAX_ROW_BYTES 4096U
static unsigned checks,cases;
static uint64_t reference_rows;
#define CHECK(c,m) do { checks++; if(!(c)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m);exit(2);} } while(0)
void data_cache_invalidate_all(void) {}
int gpu_driver_geometry_contract(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,int,int);
void gpu_driver_geometry_work_reset(void);
void gpu_driver_geometry_work(uint64_t *,uint64_t *);
static int reference_geometry(uint32_t src, uint32_t dst, uint32_t width,
                      uint32_t height, uint32_t src_stride,
                      uint32_t dst_stride, int check_src, int check_overlap)
{
    uint64_t row_bytes;
    uint64_t src_last;
    uint64_t dst_last;
    uint32_t src_row = 0U;
    uint32_t dst_row = 0U;

    if (width > 0xFFFFU || height > 0xFFFFU || (dst & 1U) != 0U) {
        return GPU_EINVAL;
    }
    if (check_src && (src & 1U) != 0U) return GPU_EINVAL;

    row_bytes = (uint64_t)width * 2U;
    if (row_bytes > GPU_MAX_ROW_BYTES) return GPU_EINVAL;
    if (check_src &&
        row_bytes + (uint64_t)(src & 0xFU) > GPU_MAX_ROW_BYTES) {
        return GPU_EINVAL;
    }
    if ((dst_stride & 1U) != 0U || dst_stride < row_bytes) return GPU_EINVAL;
    if (check_src &&
        ((src_stride & 1U) != 0U || src_stride < row_bytes)) {
        return GPU_EINVAL;
    }

    /* Empty operations are legal and complete without AXI traffic. */
    if (width == 0U || height == 0U) return GPU_OK;

    dst_last = (uint64_t)dst + (uint64_t)(height - 1U) * dst_stride;
    if (dst_last + row_bytes > 0x100000000ULL) return GPU_EINVAL;
    if (check_src) {
        src_last = (uint64_t)src + (uint64_t)(height - 1U) * src_stride;
        if (src_last + row_bytes > 0x100000000ULL) return GPU_EINVAL;
    }

    if (!check_overlap) return GPU_OK;

    /* Merge the monotonic row intervals to detect true overlap. */
    while (src_row < height && dst_row < height) {
        reference_rows++;
        uint64_t sb = (uint64_t)src + (uint64_t)src_row * src_stride;
        uint64_t db = (uint64_t)dst + (uint64_t)dst_row * dst_stride;
        uint64_t se = sb + row_bytes;
        uint64_t de = db + row_bytes;
        if (sb < de && db < se) return GPU_EOVERLAP;
        if (se <= db) src_row++;
        else dst_row++;
    }
    return GPU_OK;
}
static uint32_t rng=0x1732A0C5U;
static uint32_t next(void){rng=rng*1664525U+1013904223U;return rng;}
static int compare(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,int cs,int co)
{
    int old_rc=reference_geometry(src,dst,w,h,ss,ds,cs,co);
    int new_rc=gpu_driver_geometry_contract(src,dst,w,h,ss,ds,cs,co);
    cases++;CHECK(old_rc==new_rc,"new geometry return exactly equals original row-interval implementation");
    return new_rc;
}
int main(void)
{
    CHECK(compare(0x1000U,0x1008U,2U,3U,16U,16U,1,1)==GPU_OK,"bounding overlap with real row gaps is still legal");
    CHECK(compare(0x1000U,0x1010U,2U,2U,16U,16U,1,1)==GPU_EOVERLAP,"cross-row overlap remains rejected");
    CHECK(compare(0x1000U,0x1004U,2U,3U,16U,16U,1,1)==GPU_OK,"half-open adjacent intervals are legal");
    CHECK(compare(0xFFFFFFFCU,0x1000U,2U,1U,4U,4U,1,1)==GPU_OK,"end exactly2^32 is legal");
    CHECK(compare(0xFFFFFFFEU,0x1000U,2U,1U,4U,4U,1,1)==GPU_EINVAL,"overflow cannot be bypassed by range separation");
    CHECK(compare(0x1000U,0xFFFFFFFEU,2U,1U,4U,4U,1,1)==GPU_EINVAL,"destination overflow remains rejected");
    CHECK(compare(0x1001U,0x1000U,3U,0U,6U,6U,1,1)==GPU_EINVAL,"existing pre-empty even-address validation remains");
    CHECK(compare(0x1000U,0x1000U,0U,3U,0U,0U,1,1)==GPU_OK,"empty operation remains legal even if base addresses match");
    CHECK(compare(0x1000U,0x1000000U,2U,65535U,8U,8U,1,1)==GPU_OK,"maximum protocol height remains legal");
    CHECK(compare(0x1000U,0x1000000U,2U,65536U,8U,8U,1,1)==GPU_EINVAL,"height overflow remains rejected");
    static const uint32_t widths[]={0U,1U,2U,7U,16U,19U,64U,65U,128U,129U,2040U,2041U,2048U,2049U,65535U,65536U,0xFFFFFFFFU};
    static const uint32_t heights[]={0U,1U,2U,3U,15U,16U,17U,31U,32U,33U,63U,64U,65U};
    for(unsigned i=0;i<50000U;i++){
        uint32_t w=widths[next()%(sizeof(widths)/sizeof(widths[0]))],h=heights[next()%(sizeof(heights)/sizeof(heights[0]))];
        uint32_t src=0x1000U+(next()&0x7FEU),dst;
        unsigned layout=next()%5U;
        if(layout==0U)dst=src;
        else if(layout==1U)dst=src+(next()&0x7EU);
        else if(layout==2U)dst=src+0x100000U;
        else if(layout==3U){src=0xFFFFF000U+(next()&0xFFEU);dst=0x2000U;}
        else dst=0xFFFFF000U+(next()&0xFFEU);
        uint32_t ss=w*2U+(next()&0x3EU),ds=w*2U+(next()&0x3EU);
        if(i%11U==0U)ss|=1U;
        if(i%13U==0U)ds|=1U;
        if(i%17U==0U)src|=1U;
        if(i%19U==0U)dst|=1U;
        if(i%23U==0U)ss=0xFFFFFFFEU;
        compare(src,dst,w,h,ss,ds,i%7U!=0U,i%7U!=0U);
    }
    for(unsigned ss=4U;ss<=40U;ss+=2U)for(unsigned ds=4U;ds<=40U;ds+=2U)
        for(unsigned delta=0U;delta<=64U;delta+=2U)
            compare(0x1000U,0x1000U+delta,2U,7U,ss,ds,1,1);
    CHECK(VirtualAlloc((void *)(uintptr_t)GPU_BASE,4096U,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)GPU_BASE,"host-only fake APB page allocated");
    memset((void *)(uintptr_t)GPU_BASE,0xA4,4096U);uint8_t saved[4096];memcpy(saved,(void *)(uintptr_t)GPU_BASE,4096U);
    CHECK(gpu_alpha_bitblt(0x600000U,0x200000U,2047U,2U,4094U,4096U,0xF81FU,16U)==GPU_EINVAL,"fast geometry does not bypass later Alpha source-lane checks");
    CHECK(gpu_alpha_bitblt(0x600000U,0x200000U,2047U,2U,4096U,4094U,0xF81FU,16U)==GPU_EINVAL,"fast geometry does not bypass later Alpha destination-lane checks");
    CHECK(gpu_alpha_bitblt(0x600000U,0x200000U,16U,2U,32U,32U,0xF81FU,33U)==GPU_EINVAL,"Alpha range still validated after geometry");
    CHECK(!memcmp(saved,(void *)(uintptr_t)GPU_BASE,4096U),"invalid Alpha cases perform no MMIO writes");
    GPU_STATUS=STATUS_BUSY;memcpy(saved,(void *)(uintptr_t)GPU_BASE,4096U);
    CHECK(gpu_alpha_bitblt(0x600000U,0x200000U,16U,2U,32U,32U,0xF81FU,16U)==GPU_ETIMEOUT,"fast geometry does not bypass initial busy waiting");
    CHECK(!memcmp(saved,(void *)(uintptr_t)GPU_BASE,4096U),"busy Alpha timeout preserves all live parameters");
    gpu_driver_geometry_work_reset();reference_rows=0U;
    for(unsigned i=0U;i<128U;i++)CHECK(compare(ASSET_BASE_ADDR+i*2048U,FB0_ADDR+(i%16U)*80U+(i/16U)*40U*DISPLAY_STRIDE_BYTES,19U,31U,38U,DISPLAY_STRIDE_BYTES,1,1)==GPU_OK,"128realistic fine-bullet shapes are accepted");
    uint64_t fast,rows;gpu_driver_geometry_work(&fast,&rows);
    CHECK(fast==128U && rows==0U && reference_rows==3968U,"128bullet geometry eliminates3968row checks without hardware timing claim");
    printf("GEOMETRY checks=%u cases=%u dense_calls=128 dense_reference_rows=%llu dense_candidate_rows=%llu dense_fastpaths=%llu\n",checks,cases,(unsigned long long)reference_rows,(unsigned long long)rows,(unsigned long long)fast);
    return 0;
}
#else
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production score renderer/HUD RGB565 pixel contracts. GPU command callbacks
 * and a fixed-address host RAM mapping replace hardware; no timing claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "garden_render.h"
#include "garden_assets.h"
#include "garden_ui.h"
#include "garden_actor_ui.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"

static unsigned checks,sel,scope,fail_glyph,fail_box,reject_target;
static unsigned calls,glyph_calls,fill_calls,fences;
static uint64_t dense_digest=1469598103934665603ULL;
static unsigned dense_gpu_commands,dense_gpu_frames,dense_pixel_frames;
static unsigned erase_commands,dense_erase_commands;
static uint64_t erase_pixels,dense_erase_pixels;
typedef struct { uint32_t src,dst,w,h,ss,ds; unsigned alpha; } BlitTrace;
typedef struct { uint32_t dst,w,h; uint16_t color; } FillTrace;
static BlitTrace trace_blits[256];
static FillTrace trace_fills[256];
static unsigned trace_on,trace_blit_count,trace_fill_count,fail_at_glyph;
static uint16_t expected[DISPLAY_HEIGHT][DISPLAY_WIDTH],base[DISPLAY_HEIGHT][DISPLAY_WIDTH];
static uint16_t old_pixels[DISPLAY_HEIGHT][DISPLAY_WIDTH];
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while (0)

static uint16_t *fb(unsigned b) { return (uint16_t *)(uintptr_t)(b ? FB1_ADDR:FB0_ADDR); }
uint32_t display_back_sel(void) { return sel; }
uint32_t display_back_addr(void) { return sel ? FB1_ADDR:FB0_ADDR; }
int display_check_render_target(uint32_t address)
{
    if (reject_target) return GPU_ESTATE;
    uint32_t begin=display_back_addr();
    return address>=begin && address<begin+FB_SIZE_BYTES ? GPU_OK:GPU_ESTATE;
}
void garden_render_host_fence(void) { fences++; }
void data_cache_invalidate_all(void) {}

static void command_bounds(uint32_t dst,unsigned w,unsigned h,unsigned stride)
{
    uint32_t offset=dst-display_back_addr();
    unsigned x=(offset%DISPLAY_STRIDE_BYTES)/2U,y=offset/DISPLAY_STRIDE_BYTES;
    unsigned limit=16U;
#ifndef RENDER_BATCH_REFERENCE
    if(w<=128U){limit=4096U/w;if(limit>64U)limit=64U;}
#endif
    CHECK(w>0U && h>0U && stride==DISPLAY_STRIDE_BYTES && h<=limit,"exact width-dependent row and pixel batching bound");
    CHECK(display_check_render_target(dst)==GPU_OK && x+w<=DISPLAY_WIDTH && y+h<=DISPLAY_HEIGHT,
          "every command stays in the known physical back buffer");
    if(scope==1U) CHECK(x>=GARDEN_X0 && y>=GARDEN_Y0 && x+w<=GARDEN_X0+GARDEN_WIDTH && y+h<=GARDEN_Y0+GARDEN_HEIGHT,
                         "score composites/history touch only logical field");
    if(scope==2U) CHECK(x>=56U && y>=220U && x+w<=264U && y+h<=236U,
                         "fast score API touches only 208x16 score value window");
}

int gpu_fill(uint32_t dst,uint32_t w,uint32_t h,uint32_t stride,uint16_t color)
{
    command_bounds(dst,w,h,stride); calls++;fill_calls++;
    if(!color){erase_commands++;erase_pixels+=(uint64_t)w*h;}
    if(trace_on) {
        CHECK(trace_fill_count<256U,"bounded command recording");
        trace_fills[trace_fill_count++]=(FillTrace){dst,w,h,color};
    }
    if(fail_box) { fail_box=0;return GPU_EHW; }
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++)
        *(uint16_t *)(uintptr_t)(dst+y*stride+x*2U)=color;
    return GPU_OK;
}
static int blit(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t key,unsigned alpha)
{
    command_bounds(dst,w,h,ds);calls++;glyph_calls++;
    CHECK(src>=ASSET_BASE_ADDR && (uint64_t)src+(h-1U)*ss+w*2U<=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES,
          "actual glyph source stays inside unchanged atlas");
    CHECK(alpha<=32U && !(src&1U) && !(dst&1U),"Alpha/even-address command contract");
    if(trace_on) {
        CHECK(trace_blit_count<256U,"bounded glyph command recording");
        trace_blits[trace_blit_count++]=(BlitTrace){src,dst,w,h,ss,ds,alpha};
    }
    if(fail_at_glyph && !--fail_at_glyph) return GPU_EHW;
    if(fail_glyph) { fail_glyph=0;return GPU_EHW; }
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint16_t v=*(uint16_t *)(uintptr_t)(src+y*ss+x*2U);
        uint16_t *out=(uint16_t *)(uintptr_t)(dst+y*ds+x*2U);
        if(v!=key) *out=alpha==32U ? v:gpu_alpha_rgb565(v,*out,alpha);
    }
    return GPU_OK;
}
int gpu_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k)
{ return blit(s,d,w,h,ss,ds,k,32U); }
int gpu_alpha_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{ return blit(s,d,w,h,ss,ds,k,a); }

static void ref_box(int x,int y,int w,int h,uint16_t color)
{
    for(int row=0;row<h;row++) for(int col=0;col<w;col++) {
        int px=x+col,py=y+row;
        if(px>=GARDEN_X0 && px<GARDEN_X0+GARDEN_WIDTH && py>=GARDEN_Y0 && py<GARDEN_Y0+GARDEN_HEIGHT)
            expected[py][px]=color;
    }
}
static void ref_sprite(unsigned id,int x,int y,unsigned alpha)
{
    const garden_sprite_t *s=garden_asset(id);CHECK(s!=NULL,"oracle uses real atlas descriptor");
    x-=s->anchor_x;y-=s->anchor_y;
    for(unsigned row=0;row<s->height;row++) for(unsigned col=0;col<s->width;col++) {
        int px=x+(int)col,py=y+(int)row;
        if(px<GARDEN_X0 || px>=GARDEN_X0+GARDEN_WIDTH || py<GARDEN_Y0 || py>=GARDEN_Y0+GARDEN_HEIGHT) continue;
        uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+row*s->stride+col*2U);
        if(v!=GARDEN_KEY_COLOR) expected[py][px]=alpha==32U ? v:gpu_alpha_rgb565(v,expected[py][px],alpha);
    }
}
static void ref_bank(unsigned value,int x,int y,unsigned alpha)
{
    static const int shifts[5][6]={{-4,-3,-1,0,2,3},{-2,-1,0,0,1,1},{0,0,0,0,0,0},
                                  {2,1,0,0,-1,-1},{4,3,1,0,-2,-3}};
    static const unsigned ends[6]={4,9,14,19,24,29};
    unsigned pose=(value>>2)&7U,frame=value&3U;
    unsigned id=frame ? GARDEN_ASSET_PLAYER_FRAME1+frame-1U:GARDEN_ASSET_PLAYER;
    const garden_sprite_t *asset=garden_asset(id);
    CHECK(asset && asset->width==34 && asset->height==29 && asset->anchor_x==17 && asset->anchor_y==15,
          "independent bank oracle is bound to unchanged real 34x29 player art");
    for(unsigned row=0;row<29;row++) {
        unsigned band=0;while(row>=ends[band]) band++;
        for(unsigned col=0;col<34;col++) {
            int px=x+(int)col-17+shifts[pose][band],py=y+(int)row-15;
            if(px<GARDEN_X0 || px>=GARDEN_X0+GARDEN_WIDTH || py<GARDEN_Y0 || py>=GARDEN_Y0+GARDEN_HEIGHT) continue;
            uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+asset->offset+row*asset->stride+col*2U);
            if(v!=GARDEN_KEY_COLOR) expected[py][px]=alpha==32U ? v:gpu_alpha_rgb565(v,expected[py][px],alpha);
        }
    }
}
static void ref_bar(unsigned value,int x,int y,unsigned alpha)
{
    unsigned boss=(value&256U)!=0U,width=boss ? 128U:40U,fill=value&255U;
    uint16_t color=boss ? GARDEN_UI_CYAN:0x5FEAU,border=0xAD75U,empty=0x1084U;
    if(fill*3U<width-2U) color=0xFD20U;
    if(alpha<32U) { color=gpu_alpha_rgb565(color,0,alpha);border=gpu_alpha_rgb565(border,0,alpha);empty=gpu_alpha_rgb565(empty,0,alpha); }
    if(!alpha) return;
    /* Independent per-pixel classification, rather than production's three
     * rectangular fill commands; partial edge clipping happens in ref_box. */
    for(unsigned row=0;row<8U;row++) for(unsigned col=0;col<width;col++) {
        uint16_t v=row==0U || row==7U || col==0U || col==width-1U ? border:
                   col<=fill ? color:empty;
        ref_box(x+(int)col,y+(int)row,1,1,v);
    }
}
static void oracle_draw(const GardenDraw *d,unsigned alpha_on)
{
    unsigned alpha=alpha_on ? d->alpha:32U;
    int x=GARDEN_X0+d->x,y=GARDEN_Y0+d->y;
    if(!(d->asset&GARDEN_DRAW_SCORE_BASE)) {
        if(d->asset&GARDEN_DRAW_BAR_BASE) ref_bar(d->asset&~GARDEN_DRAW_BAR_BASE,x,y,alpha);
        else if(d->asset&GARDEN_DRAW_BANK_BASE) ref_bank(d->asset&~GARDEN_DRAW_BANK_BASE,x,y,alpha);
        else ref_sprite(d->asset,x,y,alpha);
        return;
    }
    unsigned v=d->asset&0x7FFFU;
    if(!v || !alpha) return;
    char decimal[8]; int n=snprintf(decimal,sizeof(decimal),"%u",v);
    CHECK(n>0 && n<=5,"virtual positive damage bounded to five digits");
    unsigned font=v==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE;
    uint16_t color=v==1U ? 0xFFFFU:GARDEN_UI_CYAN;
    if(alpha<32U) color=gpu_alpha_rgb565(color,0U,alpha);
    ref_box(x+2,y+7,8,2,color);
    for(int i=0;i<n;i++) {
        int id=garden_glyph(decimal[i]);CHECK(id>=0,"real score digit glyph exists");
        ref_sprite(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x+12+i*16,y,alpha);
    }
}

static void render_sequences(void)
{
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned on=0;on<2;on++) {
        garden_render_init();garden_render_alpha_support(1);
        if(!on) CHECK(garden_render_alpha_toggle()==GPU_OK,"Alpha-off fallback available");
        garden_render_mode(cpu);scope=0;
        for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"initialize both physical backgrounds");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0;tick<92U;tick++) for(unsigned b=0;b<2;b++) {
            sel=(tick+b)&1U;GardenDraw d[5];unsigned count=0;
            static const int px[9]={100,-2,640,0,635,400,100,640,100};
            static const int py[9]={100,100,478,-5,0,479,100,0,100};
            unsigned k=tick%9U,pose=tick%5U,frame=(tick/5U)%3U;
            unsigned alpha=tick%4U==0U ? 16U:tick%4U==1U ? 24U:32U;
            if(tick<90U) {
                d[count++]=(GardenDraw){(int16_t)px[k],(int16_t)py[k],(uint16_t)(0x2000U|(pose<<2)|frame),(uint8_t)alpha};
                d[count++]=(GardenDraw){(int16_t)(px[k]-20),(int16_t)(py[k]-35),(uint16_t)(0x4000U|(tick%39U)),32};
                d[count++]=(GardenDraw){(int16_t)(px[k]-64),(int16_t)(py[k]-70),(uint16_t)(0x4100U|(tick%127U)),(uint8_t)alpha};
                if(tick%2U==0U) d[count++]=(GardenDraw){(int16_t)(px[k]-10),(int16_t)(py[k]-12),0x805FU,(uint8_t)alpha};
                if(tick%3U==0U) d[count++]=(GardenDraw){(int16_t)px[k],(int16_t)py[k],GARDEN_ASSET_FOCUS_VULNERABLE,32};
            }
            memcpy(expected,base,sizeof(expected));
            for(unsigned i=0;i<count;i++) oracle_draw(&d[i],on);
            scope=1;CHECK(garden_render_frame(d,count)==GPU_OK,"production actor composite frame succeeds");scope=0;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"production CPU/GPU bank, bar and damage pixels match independent full oracle across both FB histories");
            CHECK(garden_render_stats()->sprites<=count,"one common history rectangle per bank/bar/label, never one per strip");
        }
        CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,
              "valid actor sequences never fail target/atlas checks");
    }
    puts("PASS: production five bank poses x three thruster frames, true anchor, HP bars, clipping, Alpha fallback and two-FB composite dirty erasure");
}
static void rejections(void)
{
    garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
    GardenDraw d={100,100,0x8000U,32};
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"zero virtual value rejected without pixels");
    d.asset=0x8001U;d.alpha=33;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"invalid score Alpha rejected without pixels");
    CHECK(garden_render_frame(NULL,1)==GPU_EINVAL,"non-empty null draw input rejected");
    sel=2;CHECK(garden_render_frame(NULL,0)==GPU_ESTATE,"unknown physical framebuffer rejected before history access");sel=0;
    d.alpha=32;reject_target=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"composite minus and digits enforce target guard before writing");reject_target=0;
    /* Clear failed frame history before injecting a glyph-only fault. */
    CHECK(garden_render_frame(NULL,0)==GPU_OK,"failed target history remains safely erasable");
    fail_glyph=1;CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_glyph,"actual score glyph GPU error propagates to frame result");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"failed composite retained one history rectangle and erases cleanly next frame");
    puts("PASS: zero value, invalid Alpha/null input/FB, front-target guard and GPU glyph-fault recovery");
}

static void actor_rejections(void)
{
    static const unsigned invalid[]={0x2013U,0x2014U,0x2020U,0x4080U,0x4180U,0x4200U,0x6000U};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
        GardenDraw d={100,100,(uint16_t)invalid[i],32};
        CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"malformed bank/bar domain rejected without pixels");
    }
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned kind=0;kind<2;kind++) {
        garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);garden_render_mode(cpu);memcpy(old_pixels,fb(0),sizeof(old_pixels));
        GardenDraw d={100,100,(uint16_t)(kind ? 0x4110U:0x2010U),32};reject_target=1;
        CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"bank/bar check every target before CPU or GPU writing");reject_target=0;
        CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"failed target composite cleanly erases history");
    }
    garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
    GardenDraw d={100,100,0x2010U,32};fail_glyph=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_glyph,"bank strip GPU failure propagated");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"partly painted bank strips retained in one dirty rect, erase without residual");
    d.asset=0x410FU;fail_box=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_box,"bar fill GPU failure propagated");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"partly painted bar erased without residual");
    puts("PASS: reserved bits/invalid bank poses/fill/Alpha and CPU/GPU target rejection; GPU strip/fill fault recovery");
}

/* Full-glyph oracle remains untrimmed. These tests derive ink bounds by
 * scanning actual source pixels, then independently inspect GPU commands. */
static void atlas_ink(unsigned id,unsigned *left,unsigned *top,unsigned *width,unsigned *height)
{
    const garden_sprite_t *a=garden_asset(id);
    CHECK(a && a->width==16U && a->height==16U && a->stride==32U && !a->anchor_x && !a->anchor_y,
          "trim contract binds unchanged16x16font,32byte source stride and original anchor");
    unsigned xmin=16U,ymin=16U,xmax=0U,ymax=0U,found=0U;
    for(unsigned y=0;y<16U;y++) for(unsigned x=0;x<16U;x++) {
        uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+a->offset+y*32U+x*2U);
        if(v==GARDEN_KEY_COLOR) continue;
        if(x<xmin) xmin=x;
        if(y<ymin) ymin=y;
        if(x>xmax) xmax=x;
        if(y>ymax) ymax=y;
        found=1U;
    }
    CHECK(found,"each decimal digit has actual non-Key pixels");
    *left=xmin;*top=ymin;*width=xmax-xmin+1U;*height=ymax-ymin+1U;
    for(unsigned y=0;y<16U;y++) for(unsigned x=0;x<16U;x++)
        if(x<xmin || x>xmax || y<ymin || y>ymax)
            CHECK(*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+a->offset+y*32U+x*2U)==GARDEN_KEY_COLOR,
                  "every omitted source-margin pixel is really Key, no scaling or lost font stroke");
}
static void trimmed_digits(void)
{
    for(unsigned digit=0;digit<10U;digit++) {
        unsigned l[2],t[2],w[2],h[2];
        for(unsigned style=0;style<2U;style++) {
            unsigned font=style ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE:GARDEN_ASSET_FONT_LARGE_WHITE_BASE;
            unsigned glyph=(unsigned)garden_glyph((char)('0'+digit))-GARDEN_ASSET_FONT_WHITE_BASE;
            atlas_ink(font+glyph,&l[style],&t[style],&w[style],&h[style]);
        }
        CHECK(l[0]==l[1] && t[0]==t[1] && w[0]==w[1] && h[0]==h[1],"all20white/cyan glyphs have identical independently derived ink masks");
    }
    static const unsigned values[]={1U,10U,11U,2U,3U,4U,5U,6U,7U,8U,9U,95U,190U,32767U};
    for(unsigned cpu=0;cpu<2U;cpu++) for(unsigned on=0;on<2U;on++) {
        garden_render_init();garden_render_alpha_support(1);
        if(!on) garden_render_alpha_toggle();
        garden_render_mode(cpu);scope=0U;
        for(sel=0;sel<2U;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"trim suite initializes both histories");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++) for(unsigned b=0;b<2U;b++) {
            sel=b;GardenDraw d={100,100,(uint16_t)(GARDEN_DRAW_SCORE_BASE|values[v]),16};
            memcpy(expected,base,sizeof(expected));oracle_draw(&d,on);
            trace_on=1U;trace_blit_count=trace_fill_count=0U;scope=1U;
            CHECK(garden_render_frame(&d,1U)==GPU_OK,"actual trimmedfont CPU/GPU draw succeeds");scope=0U;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"trimmedfont equals full16x16 source oracle, including real destination Alpha");
            CHECK(garden_render_stats()->sprites==1U,"minus and all digits retain one common history entry");
            char decimal[8];unsigned digits=(unsigned)snprintf(decimal,sizeof(decimal),"%u",values[v]);
            int ink_left=GARDEN_X0+102,ink_top=GARDEN_Y0+107,ink_right=GARDEN_X0+110,ink_bottom=GARDEN_Y0+109;
            unsigned transferred=0U;
            for(unsigned index=0;index<digits;index++) {
                unsigned id=(values[v]==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE)+
                    (unsigned)garden_glyph(decimal[index])-GARDEN_ASSET_FONT_WHITE_BASE;
                const garden_sprite_t *a=garden_asset(id);unsigned l,t,w,h;
                atlas_ink(id,&l,&t,&w,&h);
                int x=GARDEN_X0+112+(int)index*16+(int)l,y=GARDEN_Y0+100+(int)t;
                if(x<ink_left) ink_left=x;
                if(y<ink_top) ink_top=y;
                if(x+(int)w>ink_right) ink_right=x+(int)w;
                if(y+(int)h>ink_bottom) ink_bottom=y+(int)h;
                if(!cpu) {
                    CHECK(trace_blit_count==digits,"trimming never expands the number of digit commands");
                    const BlitTrace *bt=&trace_blits[index];
                    CHECK(bt->src==ASSET_BASE_ADDR+a->offset+t*a->stride+l*2U &&
                          bt->dst==display_back_addr()+(uint32_t)y*DISPLAY_STRIDE_BYTES+(uint32_t)x*2U &&
                          bt->w==w && bt->h==h && bt->ss==32U && bt->ds==DISPLAY_STRIDE_BYTES &&
                          bt->alpha==(on ? 16U:32U),"actual command trims only Key margins, preserves source stride and exact output coordinates/Alpha");
                    transferred+=bt->w*bt->h;
                }
            }
            if(!cpu) {
                CHECK(transferred<digits*256U,"actual GPU source rectangles transfer fewer pixels without reducing font size");
                if(values[v]==95U) CHECK(transferred==280U,"real -95 digit traffic512->280px,45.3125percent source/destination blend rectangle reduction");
            }
            trace_blit_count=trace_fill_count=0U;scope=1U;
            CHECK(garden_render_frame(NULL,0)==GPU_OK,"clear optimized composite on same physical framebuffer");scope=0U;
            CHECK(!memcmp(base,fb(sel),sizeof(base)),"tightened history erases every actual font/sign pixel with no residual");
            if(!cpu) {
                CHECK(trace_fill_count && trace_fills[0].dst==display_back_addr()+(uint32_t)ink_top*DISPLAY_STRIDE_BYTES+(uint32_t)ink_left*2U &&
                      trace_fills[0].w==(unsigned)(ink_right-ink_left) && trace_fills[0].h==(unsigned)(ink_bottom-ink_top) &&
                      trace_fills[0].color==0U,"one erase command is exactly the minus/all-digit ink union, not shrunken text or one rect per digit");
                if(values[v]==95U) CHECK(trace_fills[0].w*trace_fills[0].h==532U,"real -95 history704->532px,24.4318percent fill reduction");
            }
            trace_on=0U;
        }
    }
    puts("PASS: all20actual font ink masks; trimmed CPU/GPU digits equal full-font oracle; -95 GPU blend512->280px, history704->532px; spacing/font size unchanged");
}
static void trimmed_edges_and_overlap(void)
{
    for(unsigned cpu=0;cpu<2U;cpu++) for(unsigned on=0;on<2U;on++) {
        garden_render_init();garden_render_alpha_support(1);if(!on) garden_render_alpha_toggle();
        garden_render_mode(cpu);scope=0U;
        for(sel=0;sel<2U;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"trim stress initializes both FBs");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0;tick<105U;tick++) for(unsigned b=0;b<2U;b++) {
            sel=(tick+b)&1U;GardenDraw d[35];unsigned n=0;
            static const int xs[]={-92,-44,-38,-12,-4,0,600,630,639};
            static const int ys[]={-16,-14,-8,0,466,478,480};
            if(tick<63U) {
                d[n++]=(GardenDraw){(int16_t)xs[tick%9U],(int16_t)ys[tick%7U],0xFFFFU,16};
                d[n++]=(GardenDraw){(int16_t)(xs[tick%9U]+14),(int16_t)(ys[tick%7U]+14),0x805FU,24};
            } else if(tick<103U) {
                d[n++]=(GardenDraw){320,200,GARDEN_ASSET_BOSS1,32};
                d[n++]=(GardenDraw){300,350,0x2010U,32};
                d[n++]=(GardenDraw){256,130,0x4140U,32};
                static const unsigned value[5]={1U,95U,48U,205U,32767U};
                for(unsigned i=0;i<32U;i++) d[n++]=(GardenDraw){(int16_t)(280+(i%4U)*5U),(int16_t)(170+(i%3U)*2U-(tick-63U)/4U),
                    (uint16_t)(0x8000U|value[i%5U]),(uint8_t)(1U+(i+tick)%32U)};
            }
            memcpy(expected,base,sizeof(expected));for(unsigned i=0;i<n;i++) oracle_draw(&d[i],on);
            scope=1U;CHECK(garden_render_frame(d,n)==GPU_OK,"clipped/32overlapping independent damage composites succeed");scope=0U;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"tight history/croppedfont equals full atlas oracle for all edges, repeated changing widths, moving/fading32-label overlap on alternating FBs");
            CHECK(garden_render_stats()->sprites<=n,"32labels each use at most one dirty-history entry");
        }
        CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,"trim stress has no target/atlas/error faults");
    }
    garden_render_init();sel=0U;scope=0U;garden_render_frame(NULL,0);memcpy(base,fb(0),sizeof(base));
    GardenDraw d={200,200,0xFFFFU,16};fail_at_glyph=2U;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_at_glyph,"actual second trimmed digit fails after earlier partial painting");
    CHECK(garden_render_stats()->sprites==1U,"failed composite retains exactly one attempted ink-union history");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"tight ink union also erases a partially failed multi-digit Alpha command sequence");
    puts("PASS: cropped source/target/watchdog guards, all field-edge clipping,32overlapping labels, two-FB dirty erasure and partial multi-digit GPU failure cleanup");
}


static void batch_boxes(void)
{
    static const unsigned widths[]={1U,16U,64U,65U,83U,128U,129U,256U};
    static const unsigned heights[]={1U,16U,17U,32U,33U,64U,65U,117U,129U};
    garden_render_init();sel=0U;scope=0U;garden_render_frame(NULL,0U);
    for(unsigned wi=0;wi<sizeof(widths)/sizeof(widths[0]);wi++)for(unsigned hi=0;hi<sizeof(heights)/sizeof(heights[0]);hi++){
        unsigned w=widths[wi],h=heights[hi],band=16U;
#ifndef RENDER_BATCH_REFERENCE
        if(w<=128U){band=4096U/w;if(band>64U)band=64U;}
#endif
        trace_on=1U;trace_fill_count=0U;
        garden_render_box(200,200,(int)w,(int)h,0x3456U);
        CHECK(trace_fill_count==(h+band-1U)/band,"literal narrow/wide geometry uses exact expected command count");
        unsigned y=0U;
        for(unsigned i=0;i<trace_fill_count;i++){
            unsigned rows=h-y;if(rows>band)rows=band;
            CHECK(trace_fills[i].dst==FB0_ADDR+(200U+y)*DISPLAY_STRIDE_BYTES+400U && trace_fills[i].w==w && trace_fills[i].h==rows,"all consecutive bands exactly partition requested area");y+=rows;
        }
        for(unsigned py=199U;py<=200U+h;py++)for(unsigned px=199U;px<=200U+w;px++){
            if(py>=200U && py<200U+h && px>=200U && px<200U+w)
                CHECK(fb(0)[py*DISPLAY_WIDTH+px]==0x3456U,"every requested fill pixel written");
        }
        trace_on=0U;
    }
    /* Post-clip width controls batching, including129 ->128 and empty clips. */
    trace_on=1U;trace_fill_count=0U;garden_render_box(-1,300,129,65,0x1122U);
    unsigned band=16U;
#ifndef RENDER_BATCH_REFERENCE
    band=32U;
#endif
    CHECK(trace_fill_count==(65U+band-1U)/band && trace_fills[0].w==128U,"left clipping is applied before width policy");
    trace_fill_count=0U;garden_render_box(-129,300,129,65,0x1122U);
    CHECK(trace_fill_count==0U,"empty post-clip rectangles never divide by zero or issue commands");trace_on=0U;
}
static void dense_batches(void)
{
    for(unsigned cpu=0U;cpu<2U;cpu++)for(unsigned on=0U;on<2U;on++){
        garden_render_init();garden_render_alpha_support(1);if(!on)garden_render_alpha_toggle();garden_render_mode(cpu);scope=0U;
        for(sel=0U;sel<2U;sel++)CHECK(garden_render_frame(NULL,0U)==GPU_OK,"dense case initializes two physical backgrounds");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0U;tick<26U;tick++){
            GardenDraw draws[140];unsigned n=0U;
            if(tick<24U){
                for(unsigned i=0U;i<128U;i++) {
                    int x=tick<8U ? 320+((int)(i%16U)-8)*(int)(tick+1U)/2:(int)((i%16U)*40U+(tick%7U))-3;
                    int y=tick<8U ? 140+((int)(i/16U)-4)*(int)(tick+1U)/2:(int)(24U+(i/16U)*54U+(tick%5U));
                    draws[n++]=(GardenDraw){(int16_t)x,(int16_t)y,
                        (uint16_t)garden_bullet_precision_id(1U+(i&1U),1U+i%5U,((i*7U+tick*3U)&63U)*16U),(uint8_t)(i%3U?32U:16U)};
                }
                draws[n++]=(GardenDraw){320,121,GARDEN_ASSET_BOSS1,32};
                draws[n++]=(GardenDraw){265,120,GARDEN_ASSET_BOSS_WING_L,24};
                draws[n++]=(GardenDraw){375,120,GARDEN_ASSET_BOSS_WING_R,24};
                draws[n++]=(GardenDraw){320,121,GARDEN_ASSET_BOSS_CORE,20};
                draws[n++]=(GardenDraw){300,420,0x2010U,32};
                draws[n++]=(GardenDraw){256,50,0x4170U,32};
                draws[n++]=(GardenDraw){290,36,0x805FU,(uint8_t)(tick+1U)};
                draws[n++]=(GardenDraw){318,420,GARDEN_ASSET_FOCUS_VULNERABLE,32};
            }
            sel=tick&1U;memcpy(expected,base,sizeof(expected));for(unsigned i=0U;i<n;i++)oracle_draw(&draws[i],on);
            unsigned before=calls,erase_before=erase_commands;uint64_t pixels_before=erase_pixels;
            scope=1U;CHECK(garden_render_frame(draws,n)==GPU_OK,"128bullet+Boss/wing/core/bank/bar/damage dense frame succeeds");scope=0U;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"dense CPU/GPU batching equals independent clean redraw on alternating FBs including retirement");
            CHECK(garden_render_stats()->sprites<=n,"batching preserves one dirty history entry per logical object");
            if(!cpu&&tick<24U){dense_gpu_commands+=calls-before;dense_gpu_frames++;
                dense_erase_commands+=erase_commands-erase_before;dense_erase_pixels+=erase_pixels-pixels_before;}
            dense_pixel_frames++;
            const uint16_t *image=fb(sel);
            for(unsigned y=GARDEN_Y0;y<GARDEN_Y0+GARDEN_HEIGHT;y++)for(unsigned x=GARDEN_X0;x<GARDEN_X0+GARDEN_WIDTH;x++)
                dense_digest=(dense_digest^image[y*DISPLAY_WIDTH+x])*1099511628211ULL;
        }
    }
    for(unsigned fail=1U;fail<=2U;fail++){
        garden_render_init();garden_render_alpha_support(1);scope=0U;
        for(sel=0U;sel<2U;sel++) garden_render_frame(NULL,0U);
        memcpy(base,fb(0),sizeof(base));
        sel=0U;GardenDraw boss={320,121,GARDEN_ASSET_BOSS1,16};fail_at_glyph=fail;
        CHECK(garden_render_frame(&boss,1U)==GPU_EHW && !fail_at_glyph,"first/second actual larger-band GPU fault is propagated");
        CHECK(garden_render_stats()->sprites==1U,"faulted multi-band sprite retains complete attempted dirty rectangle");
        sel=1U;CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(1),sizeof(base)),"other FB history unaffected by partial command fault");
        sel=0U;CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"complete attempted region erased when original physical FB returns");
    }
    printf("BATCH dense_frames=%u dense_gpu_frames=%u dense_gpu_commands=%u dense_erase_commands=%u dense_erase_pixels=%llu pixel_digest=0x%016llX\n",dense_pixel_frames,dense_gpu_frames,dense_gpu_commands,dense_erase_commands,(unsigned long long)dense_erase_pixels,(unsigned long long)dense_digest);
}
static void merge_budget_cases(void)
{
    garden_render_init();garden_render_alpha_support(1);scope=0U;
    for(sel=0U;sel<2U;sel++)CHECK(garden_render_frame(NULL,0U)==GPU_OK,"merge budget initializes both FBs");
    memcpy(base,fb(0),sizeof(base));
    /* Width133 union would have8wide16-row fills vs6narrow fills: reject. */
    GardenDraw two[2]={{320,121,GARDEN_ASSET_BOSS1,16},{370,121,GARDEN_ASSET_BOSS1,24}};
    sel=0U;CHECK(garden_render_frame(two,2U)==GPU_OK,"two tall overlapping narrow sources rendered");
    unsigned before=erase_commands;uint64_t pixels_before=erase_pixels;
    CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"width128 threshold case retires with exact background");
    CHECK(erase_commands-before==6U && erase_pixels-pixels_before==2U*83U*117U,"area-legal but more-bands union is refused");
    GardenDraw full[240];
    for(unsigned i=0U;i<240U;i++)full[i]=(GardenDraw){320,121,GARDEN_ASSET_BOSS1,32};
    for(sel=0U;sel<2U;sel++)CHECK(garden_render_frame(full,240U)==GPU_OK,"maximum240overlapping history entries accepted on both FBs");
    sel=0U;before=erase_commands;pixels_before=erase_pixels;
    CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"240overlapping attempted histories erase every pixel and restore stars");
    unsigned overlap_commands=erase_commands-before;uint64_t overlap_pixels=erase_pixels-pixels_before;
#ifdef RENDER_MERGE_REFERENCE
    CHECK(overlap_commands==720U && overlap_pixels==240U*83U*117U,"frozen reference repeats all240three-band erases");
#else
    CHECK(overlap_commands==3U && overlap_pixels==83U*117U,"candidate merges240histories to one bounded three-band erase with no extra pixels");
#endif
    sel=1U;CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(1),sizeof(base)),"other framebuffer keeps its own complete old history");
    unsigned font=(unsigned)garden_glyph('I');
    for(unsigned i=0U;i<240U;i++)full[i]=(GardenDraw){(int16_t)((i%40U)*16U),(int16_t)(20U+(i/40U)*70U),(uint16_t)font,32};
    sel=0U;CHECK(garden_render_frame(full,240U)==GPU_OK,"maximum240widely separated glyphs cover bounded sparse comparison case");
    before=erase_commands;pixels_before=erase_pixels;
    CHECK(garden_render_frame(NULL,0U)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"sparse non-mergeable histories retain exact background");
    CHECK(erase_commands-before==240U && erase_pixels-pixels_before==240U*64U,"sparse separated erases do not expand area or change command budget");
    printf("MERGE_BUDGET max_history=240 comparison_bound=28680 overlap_commands=%u overlap_pixels=%llu sparse_commands=240 threshold_guard=1\n",overlap_commands,(unsigned long long)overlap_pixels);
}

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,(ASSET_BASE_ADDR + ASSET_SIZE_BYTES - FB0_ADDR),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"fixed-address host mapping covers exact production FB/atlas pointers");
    FILE *in=fopen(argv[1],"rb");CHECK(in!=NULL,"real atlas opened");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES && fgetc(in)==EOF,"exact unchanged atlas loaded");fclose(in);
    batch_boxes();dense_batches();render_sequences();rejections();
    actor_rejections();trimmed_digits();trimmed_edges_and_overlap();merge_budget_cases();
    CHECK(fences>0,"production CPU frame path exercised host fence callback");
    printf("RENDER checks=%u\n",checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}

#endif
