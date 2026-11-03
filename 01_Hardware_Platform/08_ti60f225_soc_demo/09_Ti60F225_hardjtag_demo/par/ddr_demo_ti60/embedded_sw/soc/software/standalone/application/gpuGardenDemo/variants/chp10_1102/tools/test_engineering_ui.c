/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production UI logic/layout contracts, no GPU/MMIO/hardware. Text callbacks
 * keep complete strings and model glyph rectangles at actual8/16px advances.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_ui.h"
#include "garden_render.h"

static uint16_t pixels[2][720][1280];
static unsigned sel,checks,failures,calls,text_calls,strict_hud,field_writes,side_overflows;
typedef struct {int x,y;unsigned style;char text[128];} TextCall;
static TextCall texts[256];
#define CHECK(c,m) do {checks++;if(!(c)){failures++;fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m);}}while(0)

static void mark(int x,int y,int w,int h,uint16_t color)
{
    if(strict_hud) {
        if(x<GARDEN_X0+GARDEN_WIDTH && x+w>GARDEN_X0 && y<GARDEN_Y0+GARDEN_HEIGHT && y+h>GARDEN_Y0)field_writes++;
        if(x<320 && (x<32 || x+w>288))side_overflows++;
        if(x>=960 && (x<992 || x+w>1248))side_overflows++;
    }
    CHECK(x>=0 && y>=0 && x+w<=1280 && y+h<=720 && w>=0 && h>=0,"all actual box/glyph rectangles stay on1280x720");
    for(int r=0;r<h;r++)for(int c=0;c<w;c++)if(x+c>=0 && x+c<1280 && y+r>=0 && y+r<720)
        pixels[sel][y+r][x+c]=color;
}

void garden_render_box(int x,int y,int w,int h,uint16_t color)
{calls++;mark(x,y,w,h,color);}
void garden_render_text_style(int x,int y,const char *s,unsigned style)
{
    CHECK(text_calls<256U,"bounded host text trace");if(text_calls>=256U)return;
    TextCall *t=&texts[text_calls++];t->x=x;t->y=y;t->style=style;
    CHECK(strlen(s)<sizeof(t->text),"record complete UI string without truncation");
    snprintf(t->text,sizeof(t->text),"%s",s);calls++;
    int advance=style>=GARDEN_TEXT_LARGE_WHITE ?16:8;
    for(unsigned i=0;s[i];i++)if(s[i]!=' ')mark(x+(int)i*advance,y,advance,advance,(uint16_t)(0xF000U+(unsigned char)s[i]));
}

static void trace_reset(void){calls=text_calls=0U;}
static unsigned has_text(const char *s)
{for(unsigned i=0;i<text_calls;i++)if(!strcmp(texts[i].text,s))return 1U;return 0U;}
static const TextCall *text_at(int x,int y)
{for(unsigned i=0;i<text_calls;i++)if(texts[i].x==x && texts[i].y==y)return &texts[i];return NULL;}
static unsigned text_is(int x,int y,const char *s)
{const TextCall *t=text_at(x,y);return t && !strcmp(t->text,s);}
static const char *text_value(int x,int y)
{const TextCall *t=text_at(x,y);return t ? t->text:"<missing>";}
static unsigned region_is(int x,int y,int w,int h,uint16_t color)
{for(int r=0;r<h;r++)for(int c=0;c<w;c++)if(pixels[sel][y+r][x+c]!=color)return 0U;return 1U;}

static GardenHud initial(void)
{
    GardenHud h;memset(&h,0,sizeof(h));h.state="STAGE 1 WAVES";
    h.lives=3U;h.fps10=601U;h.gpu_bench_us=23000U;h.cpu_bench_us=112000U;
    h.demo=1U;h.alpha_supported=h.alpha_enabled=1U;h.alpha_level=16U;
    h.bullets=60U;h.peak=76U;
    return h;
}

static void cache_contract(void)
{
    GardenHud h=initial(),old=h;garden_ui_hud_reset();strict_hud=1U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(has_text("STAGE 1 WAVES") && text_is(56,220,"00000000") &&
              text_is(176,432,"60.1") && text_is(176,450,"23000") &&
              text_is(176,468,"112000") && text_is(176,486,"4.86X"),
              "each physicalFB initializes actual engineering values at originalQanchors");
        CHECK(has_text("FPGA GPU / AUTO DEMO") && has_text("ALPHA / ONLINE"),
              "backend/demo/Alpha statuses reflect supplied actual fields");
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==0U,"sameHUD draws nothing independently on both physicalFBs");
    }
    h.score=111U;sel=0U;trace_reset();garden_ui_score_cached(&h,0U);
    CHECK(calls==2U && text_calls==1U && text_is(56,220,"00000111"),
          "score-only paints original56,220window and nothing else");
    trace_reset();garden_ui_hud_cached(&h,0U);
    CHECK(calls==0U,"fullHUD does not repeat already-fast-updated score or unchanged fields");
    sel=1U;trace_reset();garden_ui_hud_cached(&old,1U);
    CHECK(calls==0U,"FB0score update does not alterFB1cache");
    trace_reset();garden_ui_score_cached(&h,1U);
    CHECK(calls==2U && text_is(56,220,"00000111"),"FB1needs its own score refresh");
    trace_reset();garden_ui_score_cached(&h,1U);CHECK(calls==0U,"unchanged fastscore is no-op");
    garden_ui_hud_reset();sel=0U;trace_reset();garden_ui_score_cached(&h,0U);
    trace_reset();garden_ui_hud_cached(&h,0U);
    CHECK(has_text("STAGE 1 WAVES") && text_is(176,432,"60.1"),
          "early score fastpath does not consume full engineering initialization");
    trace_reset();garden_ui_hud_cached(&h,99U);
    CHECK(calls>0U && has_text("STAGE 1 WAVES"),"unknownFB triggers full conservative redraw");
    trace_reset();garden_ui_hud_cached(&h,99U);CHECK(calls>0U,"unknownFB is never cached as known identity");
    sel=0U;trace_reset();garden_ui_hud_cached(&h,0U);
    CHECK(calls>0U,"unknownFB invalidates physical caches");
    CHECK(field_writes==0U && side_overflows==0U,"normal dynamic engineeringHUD stays in side rails");
}

static void dynamic_contract(void)
{
    GardenHud h=initial();garden_ui_hud_reset();strict_hud=1U;
    for(sel=0;sel<2U;sel++)garden_ui_hud_cached(&h,sel);
    h.fps10=599U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==2U && text_calls==1U && text_is(176,432,"59.9"),
              "FPS updates one actual field on eachFB");
    }
    h.gpu_bench_us=21000U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==4U && text_calls==2U && text_is(176,450,"21000") &&
              text_is(176,486,"5.33X"),"GPUbench updates itself and correctly recomputed ratio only");
    }
    h.cpu_bench_us=63000U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==4U && text_calls==2U && text_is(176,468,"63000") &&
              text_is(176,486,"3.00X"),"CPUbench updates itself and correct two-decimal ratio only");
    }
    h.errors=7U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==2U && text_calls==1U && text_is(176,516,"7"),
              "actual error counter updates one field only");
    }
    h.cpu=1U;h.demo=0U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==2U && has_text("CPU / MANUAL FLIGHT"),"actual backend/mode change has truthful text");
    }
    /* Engineering metrics intentionally remain visible regardless ofIstate.
     * New core rewards are preserved in the header, not falsely shown here. */
    h.diagnostics=1U;h.high_score=999U;h.bombs=1U;h.graze=3U;h.items=2U;h.phase=2U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls==0U,"reward-only/diagnostics-flag changes do not rewrite permanent engineering panel");
    }
    h.gpu_bench_us=0U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(176,450,"0") && text_is(176,486,"N/A"),
              "unmeasured benchmark ratio remains N/A without obsolete B hint or division");
    }
}

static void maximum_contract(void)
{
    GardenHud h=initial();garden_ui_hud_reset();strict_hud=1U;
    for(sel=0;sel<2U;sel++)garden_ui_hud_cached(&h,sel);
    h.score=h.lives=h.bullets=h.peak=h.fps10=h.gpu_bench_us=h.cpu_bench_us=h.errors=UINT32_MAX;
    unsigned over_before=side_overflows;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(56,220,"4294967295") && text_is(56,332,"4294967295") &&
              text_is(168,332,"4294967295"),"score/bullet/peak preserve full ten-digit values");
        CHECK(text_is(232,262,"MAX"),"32pixel hull value explicitly uses boundedMAX");
        CHECK(text_is(176,432,"429496729.5") && text_is(176,450,"4294967295") &&
              text_is(176,468,"4294967295") && text_is(176,516,"4294967295"),
              "all diagnostic counters preserve complete maxuint32strings");
        CHECK(text_is(176,486,"1.00X"),"large equal benchmarks compute exactly1.00X");
        CHECK(region_is(232,270,32,8,GARDEN_UI_PANEL),
              "large-to-smallMAX erases old16pixel hull lowerhalf");
        printf("ENGINEERING_MAX fb=%u score=%s hull=%s bullets=%s peak=%s fps=%s gpu=%s cpu=%s speedup=%s errors=%s\n",
            sel,text_value(56,220),text_value(232,262),text_value(56,332),text_value(168,332),
            text_value(176,432),text_value(176,450),text_value(176,468),text_value(176,486),text_value(176,516));
    }
    CHECK(side_overflows==over_before,"all maxvalues remain within side-panel bounds");
    h.lives=1U;h.fps10=10U;h.bullets=h.peak=h.errors=1U;h.gpu_bench_us=h.cpu_bench_us=1U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(232,262,"1") && text_is(176,432,"1.0"),"normal glyph sizes recover after maxvalues");
        CHECK(region_is(248,262,16,16,GARDEN_UI_PANEL),"hullMAX-to1leaves no trailing characters");
        CHECK(region_is(200,432,64,8,GARDEN_UI_PANEL),"shorterFPS clears all old trailing digits");
        CHECK(region_is(184,450,80,8,GARDEN_UI_PANEL) &&
              region_is(184,468,80,8,GARDEN_UI_PANEL),"shorter benchmarks clear old trailing digits");
    }
    h.gpu_bench_us=1U;h.cpu_bench_us=UINT32_MAX;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(176,486,"OVER 99999X"),"large ratio uses bounded explicitOVERtext");
    }
    CHECK(field_writes==0U && side_overflows==0U,"all engineering HUD calls remain outside combat field and inside panels");
    trace_reset();garden_ui_hud_cached(NULL,0U);garden_ui_score_cached(NULL,0U);garden_ui_boss_cached(NULL,0U);
    CHECK(calls==0U,"nullHUD is safe");
}

int main(void)
{
    strict_hud=0U;garden_ui_hud_reset();
    for(sel=0;sel<2U;sel++)garden_ui_static();
    CHECK(has_text("FLIGHT DATA") && has_text("PERFORMANCE") &&
          has_text("JOY-CON / INPUT") && has_text("TARGET DATA") &&
          has_text("THROW BOMB") && has_text("3 BOSS TEST") && has_text("RESTART") &&
          has_text("AUTO FIRE / 10S IDLE AUTO"),"engineering panels show actual Joy-Con mappings and idle takeover");
    trace_reset();cache_contract();dynamic_contract();maximum_contract();
    printf("ENGINEERING_UI checks=%u failures=%u field_writes=%u side_overflows=%u\n",
           checks,failures,field_writes,side_overflows);
    return failures ?2:0;
}

