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
    GardenHud h;memset(&h,0,sizeof(h));h.state="WAVES";h.lives=3U;h.bombs=2U;
    h.fps10=601U;h.gpu_bench_us=23000U;h.cpu_bench_us=112000U;h.demo=1U;h.diagnostics=1U;
    return h;
}

static void cache_contract(void)
{
    GardenHud h=initial(),old=h;garden_ui_hud_reset();strict_hud=1U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(calls>0U && has_text("WAVES") && has_text("DIAGNOSTICS") && has_text("60.1") && has_text("23000/112000"),"eachFB independently draws complete initial labels and real values");
        trace_reset();garden_ui_hud_cached(&h,sel);CHECK(calls==0U,"identicalHUD submits no draws per physicalFB");
    }
    sel=0;h.score=111U;trace_reset();garden_ui_score_cached(&h,0U);
    CHECK(calls==2U && text_calls==1U && has_text("00000111"),"score-only refresh draws one exact value and its clear");
    CHECK(texts[0].x==56 && texts[0].y==182,"score-only touches only designated score window");
    trace_reset();garden_ui_hud_cached(&h,0U);CHECK(calls==0U,"fullHUD consumes already-updated score without redrawing it or other fields");
    sel=1;trace_reset();garden_ui_hud_cached(&old,1U);CHECK(calls==0U,"FB0score update does not invalidate FB1 old-score cache");
    trace_reset();garden_ui_score_cached(&h,1U);CHECK(calls==2U && has_text("00000111"),"FB1 receives its own independent score refresh");
    trace_reset();garden_ui_score_cached(&h,1U);CHECK(calls==0U,"same score-only refresh is a no-op");
    /* A score fast path before first HUD must not consume full initial draw. */
    garden_ui_hud_reset();sel=0U;trace_reset();garden_ui_score_cached(&h,0U);
    trace_reset();garden_ui_hud_cached(&h,0U);
    CHECK(has_text("WAVES") && has_text("DIAGNOSTICS"),"score fast-path before initialization preserves initial full HUD");
    /* Invalid identity is deliberately conservative and never aliases a
     * previously valid framebuffer without invalidating both caches. */
    trace_reset();garden_ui_hud_cached(&h,99U);CHECK(calls>0U && has_text("WAVES"),"unknownFB redraws conservatively");
    trace_reset();garden_ui_hud_cached(&h,99U);CHECK(calls>0U,"repeated unknownFB never treats guessed index as valid identity");
    sel=0U;trace_reset();garden_ui_hud_cached(&h,0U);CHECK(calls>0U,"unknownFB also invalidates guessed physicalFB0cache");
    sel=1U;trace_reset();garden_ui_hud_cached(&h,1U);CHECK(calls>0U,"unknownFB invalidates other physicalFB cache conservatively");
    sel=0U;trace_reset();garden_ui_score_cached(&h,99U);CHECK(calls>0U && has_text("00000111"),"unknownFB score fast path conservatively paints actual current score");
    trace_reset();garden_ui_hud_cached(&h,0U);CHECK(calls>0U && has_text("WAVES"),"unknownFB score fast path does not bless an assumed physical identity");
    CHECK(field_writes==0U && side_overflows==0U,"normal HUD/score callbacks stay in quiet side rails and never write combat field");
}

static void diagnostics_contract(void)
{
    GardenHud h=initial();garden_ui_hud_reset();strict_hud=1U;
    for(sel=0;sel<2U;sel++)garden_ui_hud_cached(&h,sel);
    h.diagnostics=0U;
    for(sel=0;sel<2U;sel++) {
        CHECK(!region_is(48,450,240,132,GARDEN_UI_BACKGROUND),"diagnostic text really existed before I-off");
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(region_is(48,450,240,132,GARDEN_UI_BACKGROUND),"I-off clears all old labels and values on each physicalFB");
        CHECK(text_calls==0U,"I-off does not reprint hidden diagnostic values");
        trace_reset();garden_ui_hud_cached(&h,sel);CHECK(calls==0U,"same hiddenHUD draws nothing");
    }
    h.fps10=10U;h.errors=4U;h.gpu_bench_us=2U;h.cpu_bench_us=7U;
    for(sel=0;sel<2U;sel++) {trace_reset();garden_ui_hud_cached(&h,sel);CHECK(calls==0U,"hidden diagnostic value changes submit no draws");}
    h.diagnostics=1U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(has_text("DIAGNOSTICS") && has_text("1.0") && has_text("2/7") && has_text("4"),"I-on restores labels and current values on bothFBs");
    }
    CHECK(field_writes==0U,"diagnostic toggles never paint combat field");
}

static void maximum_contract(void)
{
    GardenHud h=initial();h.score=h.high_score=h.lives=h.bombs=h.graze=h.items=UINT32_MAX;
    h.fps10=h.gpu_bench_us=h.cpu_bench_us=h.errors=UINT32_MAX;
    h.boss_active=1U;h.phase=2U;garden_ui_hud_reset();strict_hud=1U;
    unsigned over_before=side_overflows;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(has_text("4294967295") && has_text("429496729.5") && has_text("4294967295/4294967295"),"uint32max strings and slash/fraction values are complete");
        CHECK(text_is(56,182,"4294967295") && text_is(56,240,"4294967295") &&
              text_is(56,362,"4294967295") && text_is(168,362,"4294967295"),"score/session/graze/core strings preserve all ten digits at actual anchors");
        CHECK((text_is(56,292,"MAX") || text_is(56,292,"4294967295")) &&
              (text_is(152,292,"MAX") || text_is(152,292,"4294967295")),"narrow hull/bomb fields render explicitMAX or fullvalue, never an ambiguous numeric truncation");
        CHECK(region_is(56,300,64,8,GARDEN_UI_PANEL) && region_is(152,300,64,8,GARDEN_UI_PANEL),"large-to-small/MAX transition erases old16pixel glyph lower halves");
        CHECK(has_text("3"),"actual Boss phase is one-based");
        printf("MAXIMUM_FB fb=%u score=%s best=%s lives=%s bombs=%s graze=%s cores=%s fps=%s bench=%s errors=%s\n",
            sel,text_value(56,182),text_value(56,240),text_value(56,292),text_value(152,292),
            text_value(56,362),text_value(168,362),text_value(168,482),text_value(56,526),text_value(168,554));
    }
    CHECK(side_overflows==over_before,"full ten-digit values never cross side-rail outer bounds");
    CHECK(field_writes==0U,"maximum counters do not paint combat field");
    h.lives=h.bombs=99999999U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(56,292,"99999999") && text_is(152,292,"99999999"),"eight-digit hull/bomb fields use complete fallback small text");
    }
    h.lives=h.bombs=1U;
    for(sel=0;sel<2U;sel++) {
        trace_reset();garden_ui_hud_cached(&h,sel);
        CHECK(text_is(56,292,"1") && text_is(152,292,"1"),"normal small values restore readable fullglyphs");
        CHECK(region_is(72,292,48,16,GARDEN_UI_PANEL) && region_is(168,292,48,16,GARDEN_UI_PANEL),"shrinking8digits-to1digit leaves no trailing oldtext");
    }
    GardenHud n=initial();n.state=NULL;sel=0U;trace_reset();garden_ui_hud_cached(&n,0U);
    CHECK(has_text(""),"null state is supported as empty text");
    trace_reset();garden_ui_hud_cached(NULL,0U);garden_ui_score_cached(NULL,0U);garden_ui_boss_cached(NULL,0U);
    CHECK(calls==0U,"nullHUD is safe and submits no draws");
}

int main(void)
{
    garden_ui_hud_reset();strict_hud=0U;
    for(sel=0;sel<2U;sel++)garden_ui_static();
    trace_reset();cache_contract();diagnostics_contract();maximum_contract();
    printf("REWARD_UI checks=%u failures=%u field_writes=%u side_overflows=%u\n",checks,failures,field_writes,side_overflows);
    return failures ?2:0;
}
