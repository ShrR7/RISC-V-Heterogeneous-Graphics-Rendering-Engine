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
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"

static unsigned checks,sel,scope,fail_glyph,fail_box,reject_target;
static unsigned calls,glyph_calls,fill_calls,fences;
static uint16_t expected[DISPLAY_HEIGHT][DISPLAY_WIDTH],base[DISPLAY_HEIGHT][DISPLAY_WIDTH];
static uint16_t saved[2][DISPLAY_HEIGHT][DISPLAY_WIDTH],old_pixels[DISPLAY_HEIGHT][DISPLAY_WIDTH];
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
    CHECK(stride==DISPLAY_STRIDE_BYTES && h<=16U,"production GPU operations use display stride and watchdog-safe bands <=16");
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
    CHECK(alpha<=32U,"Alpha command validated in 0..32");
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
static void oracle_draw(const GardenDraw *d,unsigned alpha_on)
{
    unsigned alpha=alpha_on ? d->alpha:32U;
    int x=GARDEN_X0+d->x,y=GARDEN_Y0+d->y;
    if(!(d->asset&GARDEN_DRAW_SCORE_BASE)) { ref_sprite(d->asset,x,y,alpha);return; }
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
    CHECK(!garden_render_score_width(0U) && !garden_render_score_width(32768U),"zero/out-of-range score widths rejected");
    CHECK(garden_render_score_width(1U)==28U && garden_render_score_width(9U)==28U &&
          garden_render_score_width(10U)==44U && garden_render_score_width(32767U)==92U,"common bbox tracks digit boundaries");
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned on=0;on<2;on++) {
        garden_render_init();garden_render_alpha_support(1);
        if(!on) CHECK(garden_render_alpha_toggle()==GPU_OK,"Alpha-off fallback available");
        garden_render_mode(cpu);scope=0;
        for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"initialize each physical static background");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0;tick<36;tick++) for(unsigned b=0;b<2;b++) {
            sel=(tick+b)&1U;
            GardenDraw d[4];unsigned count=0;
            if(tick<34U) {
                static const int x[]={100,-20,630,-85,620,0,547,639,-120};
                static const int y[]={100,-8,475,120,479,0,464,200,-32};
                static const unsigned v[]={1,12,32767,1000,9,40,100,321,7};
                static const unsigned a[]={32,24,16,20,10,32,24,32,0};
                unsigned k=tick%9U;
                d[count++]=(GardenDraw){(int16_t)x[k],(int16_t)y[k],(uint16_t)(0x8000U|v[k]),(uint8_t)a[k]};
                if(tick%3U==0U) d[count++]=(GardenDraw){100,106,(uint16_t)(0x8000U|50U),20};
                if(tick%2U==0U) d[count++]=(GardenDraw){113,124,GARDEN_ASSET_PLAYER,32};
            }
            memcpy(expected,base,sizeof(expected));
            for(unsigned i=0;i<count;i++) oracle_draw(&d[i],on);
            scope=1;CHECK(garden_render_frame(d,count)==GPU_OK,"production composite frame succeeds on GPU and CPU");scope=0;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"two-buffer dirty erase equals independent full RGB565 redraw including overlap and field edges");
            CHECK(garden_render_stats()->sprites<=count,"each score composite consumes at most one history slot");
        }
        CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,
              "valid composite sequences have no renderer error/target/atlas faults");
    }
    puts("PASS: production CPU/GPU score composites, real atlas font Alpha, common bbox, edge clipping, overlap and independent two-FB dirty erase");
}

static GardenHud hud(void)
{
    GardenHud h={0};h.state="STAGE 1 BOSS";h.score=320;h.lives=3;h.bullets=48;h.peak=82;
    h.fps10=601;h.render_us=2400;h.logic_us=200;h.gpu_bench_us=9000;h.cpu_bench_us=39000;
    h.demo=1;h.alpha_supported=1;h.alpha_enabled=1;h.alpha_level=16;
    h.boss_active=1;h.boss_hp=1000;h.boss_max_hp=1000;h.boss_attack=1;return h;
}
static void full_reference(const GardenHud *h)
{
    uint16_t *out=fb(sel);memcpy(old_pixels,out,sizeof(old_pixels));
    memcpy(out,base,sizeof(base));scope=0;garden_ui_hud(h);memcpy(expected,out,sizeof(expected));
    memcpy(out,old_pixels,sizeof(old_pixels));
}
static void checked_score(const GardenHud *h,unsigned b)
{
    GardenRenderStats old=*garden_render_stats();garden_ui_score_cached(h,b);
    const GardenRenderStats *now=garden_render_stats();
    if(old.errors!=now->errors || old.bad_targets!=now->bad_targets || old.atlas_bounds!=now->atlas_bounds)
        garden_ui_hud_reset();
}
static void hud_scores(void)
{
    GardenHud h=hud(),per_buffer[2]={h,h};garden_render_init();scope=0;
    for(sel=0;sel<2;sel++) {
        garden_ui_static();memcpy(base,fb(sel),sizeof(base));
        scope=2;calls=0;garden_ui_score_cached(&h,sel);CHECK(calls>0,"partial-first score renders independently");
        scope=0;calls=0;garden_ui_hud_cached(&h,sel);CHECK(calls>0,"partial score never consumes initial full HUD");
        full_reference(&h);CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"initial cached complete HUD matches full production pixels");
    }
    static const uint32_t edge[]={321,9,10,99,100,999,1000,0,UINT32_MAX,1,320};
    for(unsigned tick=0;tick<240;tick++) for(unsigned b=0;b<2;b++) {
        sel=(tick+b)&1U;h.score=tick<sizeof(edge)/sizeof(edge[0]) ? edge[tick]:1000U+tick;
        memcpy(saved[sel],fb(sel),sizeof(saved[sel]));scope=2;calls=0;garden_ui_score_cached(&h,sel);
        CHECK(calls>0,"changed true score updates corresponding physical buffer");
        for(unsigned y=0;y<DISPLAY_HEIGHT;y++) for(unsigned x=0;x<DISPLAY_WIDTH;x++)
            if(x<56U || x>=264U || y<220U || y>=236U)
                CHECK(fb(sel)[y*DISPLAY_WIDTH+x]==saved[sel][y][x],"score-only refresh preserves every non-score HUD/playfield pixel");
        calls=0;garden_ui_score_cached(&h,sel);CHECK(!calls,"unchanged score emits no commands");scope=0;
        per_buffer[sel].score=h.score;full_reference(&per_buffer[sel]);CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"mixed fast score refresh equals complete HUD oracle, no stale digits");
        if(tick%20U==0U) {
            h.render_us+=1U;garden_ui_hud_cached(&h,sel);per_buffer[sel]=h;
            full_reference(&h);CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"slow performance HUD cooperates with independently updated score cache");
        }
    }
    /* Full HUD cached draw synchronizes the score cache without requiring a
     * later duplicate score draw. Changes to other HUD values are intentional. */
    scope=0;
    for(sel=0;sel<2;sel++) {
        h.score+=7U;garden_ui_hud_cached(&h,sel);calls=0;scope=2;garden_ui_score_cached(&h,sel);
        CHECK(!calls,"full HUD synchronizes independent score history");scope=0;
    }
    garden_ui_hud_reset();
    for(sel=0;sel<2;sel++) { calls=0;scope=2;garden_ui_score_cached(&h,sel);CHECK(calls>0,"reset invalidates both score histories");scope=0; }
    sel=0;calls=0;scope=2;garden_ui_score_cached(&h,2U);CHECK(calls>0,"unknown framebuffer draws score and invalidates both caches");scope=0;
    for(sel=0;sel<2;sel++) { calls=0;garden_ui_hud_cached(&h,sel);CHECK(calls>0,"unknown score destination requires both full HUDs to rebuild"); }
    calls=0;garden_ui_score_cached(NULL,0);CHECK(!calls,"null score input leaves renderer/cache untouched");
    for(unsigned kind=0;kind<2;kind++) {
        sel=0;h.score+=1U;scope=2;if(kind) fail_glyph=1;else fail_box=1;
        checked_score(&h,sel);CHECK(!fail_glyph && !fail_box,"requested score glyph/box fault actually injected");scope=0;
        for(sel=0;sel<2;sel++) {
            calls=0;garden_ui_hud_cached(&h,sel);CHECK(calls>0,"caller fault reset retries full HUD on each buffer");
            full_reference(&h);CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"retry repairs score after failed glyph/clear command");
        }
    }
    puts("PASS: production score-only HUD window, two independent caches, 240 mixed updates, max/zero/reset/unknown destination and caller fault retries");
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

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,(ASSET_BASE_ADDR + ASSET_SIZE_BYTES - FB0_ADDR),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"fixed-address host mapping covers exact production FB/atlas pointers");
    FILE *in=fopen(argv[1],"rb");CHECK(in!=NULL,"real atlas opened");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES && fgetc(in)==EOF,"exact unchanged atlas loaded");fclose(in);
    render_sequences();hud_scores();rejections();
    CHECK(fences>0,"production CPU frame path exercised host fence callback");
    printf("PASS: score renderer/HUD host contract checks=%u\n",checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
