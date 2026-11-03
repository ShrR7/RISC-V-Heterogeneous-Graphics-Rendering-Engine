/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production HUD contracts in RGB565 RAM with the real atlas fonts.
 * No GPU/MMIO, serial/JTAG, FPGA pixels or hardware-performance claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_ui.h"
#include "garden_render.h"
#include "garden_assets.h"

enum { WIDTH=1280, HEIGHT=720, LEFT=1016, TOP=436, RIGHT=1224, BOTTOM=508 };
static uint16_t pixels[HEIGHT][WIDTH], cached_pixels[2][HEIGHT][WIDTH];
static uint16_t static_pixels[HEIGHT][WIDTH], reference[HEIGHT][WIDTH], before[HEIGHT][WIDTH];
static unsigned char atlas[GARDEN_ATLAS_BYTES];
static unsigned checks,calls,boss_only,box_seen,glyph_seen,fail_box,fail_glyph,fail_field,injected;
static GardenRenderStats render_stats;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while (0)

const GardenRenderStats *garden_render_stats(void) { return &render_stats; }

static void bounds(int x,int y,int w,int h)
{
    CHECK(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=WIDTH && y+h<=HEIGHT,
          "RGB565 renderer primitive remains inside physical framebuffer");
    if(boss_only) CHECK(x>=LEFT && y>=TOP && x+w<=RIGHT && y+h<=BOTTOM,
                        "fast Boss API writes only Boss value windows, no FPS/performance/playfield");
}

static int fault(unsigned glyph)
{
    unsigned *seen=glyph ? &glyph_seen:&box_seen;
    unsigned *fail=glyph ? &fail_glyph:&fail_box;
    (*seen)++;
    if(!*fail || *seen!=*fail) return 0;
    *fail=0; injected++;
    if(fail_field==0) render_stats.errors++;
    else if(fail_field==1) render_stats.bad_targets++;
    else render_stats.atlas_bounds++;
    return 1;
}

void garden_render_box(int x,int y,int w,int h,uint16_t color)
{
    bounds(x,y,w,h); calls++; render_stats.commands++;
    if(fault(0)) return;
    for(int row=0;row<h;row++) for(int col=0;col<w;col++) pixels[y+row][x+col]=color;
}

static void glyph(unsigned id,int x,int y)
{
    const garden_sprite_t *s=garden_asset(id);
    CHECK(s!=NULL,"font glyph descriptor exists in production atlas");
    x-=s->anchor_x; y-=s->anchor_y; bounds(x,y,s->width,s->height);
    render_stats.commands++;
    if(fault(1)) return;
    for(unsigned row=0;row<s->height;row++) for(unsigned col=0;col<s->width;col++) {
        unsigned p=s->offset+row*s->stride+col*2U;
        CHECK(p+1U<sizeof(atlas),"font pixel stays inside actual atlas bounds");
        uint16_t v=(uint16_t)(atlas[p]|(unsigned)atlas[p+1U]<<8);
        if(v!=GARDEN_KEY_COLOR) pixels[y+(int)row][x+(int)col]=v;
    }
}

void garden_render_text_style(int x,int y,const char *s,unsigned style)
{
    calls++;
    const int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16:8;
    const int start=x;
    const unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE :
                        style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE :
                        style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE : GARDEN_ASSET_FONT_WHITE_BASE;
    while(*s) {
        int id=garden_glyph(*s);
        if(*s=='\n') { x=start; y+=advance+8; }
        else {
            CHECK(id>=0,"HUD text uses supported production glyphs");
            if(*s!=' ') glyph(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x,y);
            x+=advance;
        }
        s++;
    }
}

static GardenHud base_hud(void)
{
    GardenHud h={0};
    h.state="STAGE 1 BOSS"; h.score=320; h.lives=3; h.bullets=48; h.peak=82;
    h.fps10=601; h.render_us=2400; h.logic_us=200;
    h.gpu_bench_us=9000; h.cpu_bench_us=39000;
    h.demo=1; h.alpha_supported=1; h.alpha_enabled=1; h.alpha_level=16;
    h.boss_active=1; h.boss_hp=1000; h.boss_max_hp=1000; h.boss_attack=1;
    return h;
}

static void setup(void)
{
    boss_only=0; garden_ui_static(); memcpy(static_pixels,pixels,sizeof(pixels));
    for(unsigned b=0;b<2;b++) memcpy(cached_pixels[b],pixels,sizeof(pixels));
    garden_ui_hud_reset();
}

static void oracle(const GardenHud *h)
{
    unsigned saved=boss_only; boss_only=0;
    memcpy(pixels,static_pixels,sizeof(pixels)); garden_ui_hud(h);
    memcpy(reference,pixels,sizeof(pixels)); boss_only=saved;
}

/* Full UI cache and separate fast Boss cache share the same production oracle.
 * A 10Hz Boss update is simulated by each step; it never changes logic time.
 */
static unsigned step(const GardenHud *h,unsigned buffer,unsigned partial,int expected)
{
    CHECK(buffer<2,"physical framebuffer index is known");
    memcpy(pixels,cached_pixels[buffer],sizeof(pixels)); memcpy(before,pixels,sizeof(pixels));
    GardenHud saved=*h;
    calls=0; boss_only=partial;
    if(partial) garden_ui_boss_cached(h,buffer); else garden_ui_hud_cached(h,buffer);
    unsigned cost=calls;
    if(expected>=0) CHECK((calls>0)==(expected>0),"expected independent cache draw/cache hit");
    CHECK(!memcmp(h,&saved,sizeof(saved)),"HUD rendering leaves every input field unchanged");
    if(partial) for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++)
        if(x<LEFT || x>=RIGHT || y<TOP || y>=BOTTOM)
            CHECK(pixels[y][x]==before[y][x],"fast update preserves every non-Boss RGB565 pixel");
    memcpy(cached_pixels[buffer],pixels,sizeof(pixels));
    calls=0;
    if(partial) garden_ui_boss_cached(h,buffer); else garden_ui_hud_cached(h,buffer);
    CHECK(calls==0 && !memcmp(pixels,cached_pixels[buffer],sizeof(pixels)),
          "unchanged repeat does not issue any renderer primitive");
    oracle(h);
    CHECK(!memcmp(reference,cached_pixels[buffer],sizeof(pixels)),
          "cached complete framebuffer equals full production HUD/font oracle");
    memcpy(pixels,cached_pixels[buffer],sizeof(pixels)); boss_only=0;
    return cost;
}

static void lifecycle(void)
{
    GardenHud h=base_hud(); setup();
    for(unsigned b=0;b<2;b++) {
        calls=0; garden_ui_boss_cached(&h,b);
        CHECK(calls==0,"removed remote Boss API never renders or consumes full HUD initialization");
        step(&h,b,0,1);
    }
    for(unsigned i=0;i<40U;i++) {
        h.boss_hp=i ? UINT32_MAX/i : 0U; h.boss_max_hp=i%2U ? 0U:UINT32_MAX;
        h.boss_active=i%2U; h.boss_attack=i;
        for(unsigned b=0;b<2;b++) {
            step(&h,b,0,0);
            memcpy(pixels,cached_pixels[b],sizeof(pixels));
            for(int y=392;y<528;y++) for(int x=992;x<1248;x++)
                CHECK(pixels[y][x]==static_pixels[y][x],"right panel stays static: no HP bar, percent or pattern value");
        }
    }
    calls=0; garden_ui_boss_cached(&h,2); garden_ui_boss_cached(NULL,0);
    CHECK(calls==0,"legacy remote Boss calls never draw even for unknown buffer");
    garden_ui_hud_reset();for(unsigned b=0;b<2;b++)step(&h,b,0,1);
    puts("PASS: HP/active/attack changes never repaint the right guide panel; two-buffer HUD remains independent");
}

static void original_fields(void)
{
    GardenHud base=base_hud(); setup();
    for(unsigned b=0;b<2;b++) step(&base,b,0,1);
    char state[32]="STAGE 1 BOSS", other[32]="STAGE 1 BOSS";
    base.state=state;
    for(unsigned b=0;b<2;b++) step(&base,b,0,0);
    base.state=other;
    for(unsigned b=0;b<2;b++) step(&base,b,0,0);
    strcpy(other,"PAUSED");
    for(unsigned b=0;b<2;b++) step(&base,b,0,1);
    for(unsigned n=0;n<21;n++) {
        GardenHud h=base;
        switch(n) {
            case 0:h.score=UINT32_MAX;break; case 1:h.lives=1;break;
            case 2:h.bullets=192;break; case 3:h.peak=192;break;
            case 4:h.cpu=1;break; case 5:h.demo=0;break;
            case 6:h.fps10=9999;break; case 7:h.render_us=UINT32_MAX;break;
            case 8:h.logic_us=UINT32_MAX;break; case 9:h.gpu_bench_us=1;break;
            case 10:h.cpu_bench_us=UINT32_MAX;break; case 11:h.errors=UINT32_MAX;break;
            case 12:h.alpha_supported=0;break; case 13:h.alpha_enabled=0;break;
            case 14:h.alpha_level=8;break; case 15:h.boss_active=0;break;
            case 16:h.boss_hp=1;break; case 17:h.boss_max_hp=UINT32_MAX;break;
            case 18:h.boss_attack=3;break; case 19:h.gpu_bench_us=0;break;
            default:h.state="VICTORY / R TO PLAY";break;
        }
        for(unsigned b=0;b<2;b++) step(&h,b,0,(n==7U || n==8U || (n>=15U && n<=18U))?0:1);
        for(unsigned b=0;b<2;b++) step(&base,b,0,(n==7U || n==8U || (n>=15U && n<=18U))?0:1);
    }
    char long_name[72]; memset(long_name,' ',sizeof(long_name)-1); long_name[sizeof(long_name)-1]=0;
    GardenHud long_h=base; long_h.state=long_name;
    for(unsigned n=0;n<2;n++) {
        memcpy(pixels,cached_pixels[0],sizeof(pixels)); calls=0;
        garden_ui_hud_cached(&long_h,0); CHECK(calls>0,"overlong state always full draws instead of silently truncating cache");
    }
    garden_ui_hud_reset();
    for(unsigned b=0;b<2;b++) step(&base,b,0,1);
    puts("PASS: remaining full-HUD fields and ignored remote Boss inputs, same-text pointers, mutated state and overlength fallback retain complete two-buffer pixel contract");
}

/* The caller owns render-fault recovery: it compares the renderer's sticky
 * counters around a UI operation and invalidates both HUD caches on a change.
 * Test this actual contract rather than claiming void UI primitives can know
 * whether hardware accepted an asynchronous command.
 */
static void checked_boss_call(const GardenHud *h,unsigned b)
{
    GardenRenderStats old=*garden_render_stats();
    garden_ui_score_cached(h,b);
    const GardenRenderStats *now=garden_render_stats();
    if(old.errors!=now->errors || old.bad_targets!=now->bad_targets || old.atlas_bounds!=now->atlas_bounds)
        garden_ui_hud_reset();
}

static void fault_retries(void)
{
    GardenHud h=base_hud(); setup();
    for(unsigned b=0;b<2;b++) step(&h,b,0,1);
    for(unsigned field=0;field<3;field++) for(unsigned kind=0;kind<2;kind++) {
        h.score+=1U;
        memcpy(pixels,cached_pixels[0],sizeof(pixels)); boss_only=0;
        box_seen=glyph_seen=0; fail_field=field;
        if(kind) fail_glyph=1; else fail_box=1;
        unsigned old_injected=injected;
        checked_boss_call(&h,0);
        CHECK(injected==old_injected+1,"chosen renderer box/font error was actually injected");
        memcpy(cached_pixels[0],pixels,sizeof(pixels)); boss_only=0;
        /* Failed output must be repainted, including an initial full cache
         * that the fault wrapper invalidated. The other FB must retry too.
         */
        for(unsigned b=0;b<2;b++) step(&h,b,0,1);
    }
    puts("PASS: caller error/bad_targets/atlas_bounds recovery retries both framebuffer HUD and score caches after box/font command faults");
}

int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"Usage: test_feedback_hud ATLAS\n"); return 2; }
    FILE *in=fopen(argv[1],"rb");
    if(!in || fread(atlas,1,sizeof(atlas),in)!=sizeof(atlas) || fgetc(in)!=EOF) {
        fprintf(stderr,"Exact atlas load failed\n"); return 2;
    }
    fclose(in);
    lifecycle(); original_fields(); fault_retries();
    printf("PASS: feedback HUD host contract checks=%u\n",checks);
    return 0;
}
