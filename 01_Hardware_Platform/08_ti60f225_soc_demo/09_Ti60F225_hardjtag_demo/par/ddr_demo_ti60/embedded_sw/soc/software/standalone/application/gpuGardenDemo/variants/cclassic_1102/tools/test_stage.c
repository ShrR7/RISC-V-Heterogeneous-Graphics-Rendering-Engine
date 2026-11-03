/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production garden_stage host contracts and RGB565 footer cache oracle.
 * No UART, JTAG, framebuffer MMIO or board-performance claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_stage.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_ui.h"

enum { WIDTH=1280, HEIGHT=720, LEFT=504, TOP=634, RIGHT=948, BOTTOM=678 };
static uint16_t pixels[HEIGHT][WIDTH], cached_pixels[2][HEIGHT][WIDTH];
static uint16_t static_pixels[HEIGHT][WIDTH], reference[HEIGHT][WIDTH];
static unsigned char atlas[GARDEN_ATLAS_BYTES];
static unsigned checks, footer_only, calls;
static GardenRenderStats render_stats;
static unsigned fail_box_at, fail_glyph_at, box_seen, glyph_seen, fail_field, injected;
#define CHECK(condition,message) do { checks++; if (!(condition)) { \
    fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,message); exit(2); } } while (0)

static void bounds(int x,int y,int w,int h)
{
    CHECK(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=WIDTH && y+h<=HEIGHT,
          "render primitive remains inside physical framebuffer");
    if (footer_only) CHECK(x>=LEFT && y>=TOP && x+w<=RIGHT && y+h<=BOTTOM,
                          "stage writes only its footer interior, no playfield/UI labels");
}

const GardenRenderStats *garden_render_stats(void)
{
    return &render_stats;
}

static int inject_fault(unsigned is_glyph)
{
    unsigned *seen=is_glyph ? &glyph_seen:&box_seen;
    unsigned *target=is_glyph ? &fail_glyph_at:&fail_box_at;
    (*seen)++;
    if(!*target || *seen!=*target) return 0;
    *target=0U; injected++;
    if(fail_field==0U) render_stats.errors++;
    else if(fail_field==1U) render_stats.bad_targets++;
    else render_stats.atlas_bounds++;
    return 1;
}

void garden_render_box(int x,int y,int w,int h,uint16_t colour)
{
    bounds(x,y,w,h); calls++;
    render_stats.commands++;
    if(inject_fault(0U)) return;
    for(int row=0;row<h;row++) for(int col=0;col<w;col++) pixels[y+row][x+col]=colour;
}

static void glyph(unsigned id,int x,int y)
{
    const garden_sprite_t *s=garden_asset(id);
    CHECK(s!=NULL,"font glyph exists in accepted atlas");
    x-=s->anchor_x; y-=s->anchor_y; bounds(x,y,s->width,s->height);
    render_stats.commands++;
    if(inject_fault(1U)) return;
    for(unsigned row=0;row<s->height;row++) for(unsigned col=0;col<s->width;col++) {
        unsigned p=s->offset+row*s->stride+col*2U;
        CHECK(p+1U<sizeof(atlas),"font pixel stays inside atlas");
        uint16_t value=(uint16_t)(atlas[p]|(unsigned)atlas[p+1U]<<8);
        if(value!=GARDEN_KEY_COLOR) pixels[y+(int)row][x+(int)col]=value;
    }
}

void garden_render_text_style(int x,int y,const char *text,unsigned style)
{
    calls++;
    const int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16:8;
    const int start=x;
    const unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE :
                        style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE :
                        style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE : GARDEN_ASSET_FONT_WHITE_BASE;
    while(*text) {
        int id=garden_glyph(*text);
        if(*text=='\n') { x=start; y+=advance+8; }
        else {
            CHECK(id>=0,"stage uses supported production font glyphs");
            if(*text!=' ') glyph(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x,y);
            x+=advance;
        }
        text++;
    }
}

static void static_frame(void)
{
    footer_only=0; garden_ui_static(); memcpy(static_pixels,pixels,sizeof(pixels));
    memcpy(cached_pixels[0],pixels,sizeof(pixels)); memcpy(cached_pixels[1],pixels,sizeof(pixels));
    footer_only=1;
}

/* Force the same production module to full-redraw its footer onto a fresh
 * production static UI. Compare complete RGB565 images, so static labels and
 * the playfield are part of the oracle rather than a separately coded model.
 */
static void cache_step(GardenStage *stage,const GardenGame *game,unsigned buffer,int expected_draw)
{
    CHECK(buffer<2U,"cache test physical framebuffer index");
    memcpy(pixels,cached_pixels[buffer],sizeof(pixels));
    calls=0;
    GardenGame saved=*game;
    int changed=garden_stage_draw(stage,game,buffer);
    CHECK(!memcmp(game,&saved,sizeof(saved)),"stage draw leaves complete game unchanged");
    CHECK((changed==1)==(calls>0),"draw result matches actual renderer primitives");
    if(expected_draw>=0) CHECK(changed==expected_draw,"expected independent framebuffer redraw/cache hit");
    memcpy(cached_pixels[buffer],pixels,sizeof(pixels));
    calls=0; CHECK(garden_stage_draw(stage,game,buffer)==0 && calls==0,
                   "unchanged physical framebuffer emits no drawing calls");
    GardenStage full=*stage;
    garden_stage_ui_reset(&full);
    memcpy(pixels,static_pixels,sizeof(pixels)); calls=0;
    CHECK(garden_stage_draw(&full,game,buffer)==1 && calls>0,"full-reset production footer redraws");
    memcpy(reference,pixels,sizeof(pixels));
    CHECK(!memcmp(reference,cached_pixels[buffer],sizeof(pixels)),
          "cached footer pixels equal full production redraw across complete RGB565 framebuffer");
    memcpy(pixels,cached_pixels[buffer],sizeof(pixels));
}

static void waves(GardenGame *game,uint32_t tick)
{
    memset(game,0,sizeof(*game));
    game->tick=tick; game->mode=GARDEN_MODE_WAVES; game->lives=3;
    game->player.active=1;
}

static void start(GardenStage *stage,GardenGame *game)
{
    garden_stage_reset(stage); garden_stage_observe(stage,game);
}

static void enter(GardenStage *stage,GardenGame *game)
{
    game->tick++; game->mode=GARDEN_MODE_BOSS; game->boss.active=1;
    garden_stage_observe(stage,game);
}

static void entrance_timing(void)
{
    GardenStage stage; GardenGame game;
    waves(&game,100); start(&stage,&game); enter(&stage,&game);
    uint32_t entry=game.tick;
    CHECK(stage.stats.boss_entries==1 && stage.boss_entry_valid,"one WAVES-to-BOSS entrance detected");
    unsigned last_alpha=0;
    for(unsigned age=0;age<=100U;age++) {
        game.tick=entry+age; garden_stage_observe(&stage,&game);
        GardenStageStats before=*garden_stage_stats(&stage);
        for(unsigned repeat=0;repeat<5U;repeat++) {
            garden_stage_observe(&stage,&game);
            unsigned alpha=garden_stage_boss_alpha(&stage,&game,1);
            CHECK(alpha>=8 && alpha<=32,"Boss entrance alpha bounded8..32");
            CHECK(alpha>=last_alpha,"Boss entrance alpha rises monotonically");
            if(age==0) CHECK(alpha==8,"entrance begins at alpha8");
            if(age>=63U) CHECK(alpha==32,"fade reaches alpha32 by age63 and stays opaque");
            if(age<63U) CHECK(alpha<32,"fade does not finish before age63");
            CHECK(garden_stage_boss_alpha(&stage,&game,0)==32,"alpha-disabled Boss remains opaque");
            last_alpha=alpha;
        }
        CHECK(!memcmp(&before,garden_stage_stats(&stage),sizeof(before)),
              "same-tick observe/alpha calls do not duplicate event/fade statistics");
        for(unsigned buffer=0;buffer<2U;buffer++) {
            cache_step(&stage,&game,buffer,-1);
            CHECK(stage.cache[buffer].kind==(age<100U ? GARDEN_STAGE_WARNING:GARDEN_STAGE_FOOTER),
                  "warning lasts exactly age0..99 at40Hz");
        }
    }
    CHECK(stage.stats.boss_fade_samples==64,"exact64 active logic entrance samples");
    puts("PASS: one WAVES-to-BOSS transition, alpha8-to32 over64ticks, warning100ticks, same-tick idempotence");
}

static void pause_lifecycle(void)
{
    GardenStage stage; GardenGame game;
    waves(&game,10); start(&stage,&game); enter(&stage,&game);
    game.tick+=20; garden_stage_observe(&stage,&game);
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,-1);
    unsigned alpha=garden_stage_boss_alpha(&stage,&game,1);
    GardenStageStats saved=stage.stats;
    game.paused=1;
    for(unsigned i=0;i<100U;i++) {
        garden_stage_observe(&stage,&game);
        CHECK(garden_stage_boss_alpha(&stage,&game,1)==alpha,"pause/same tick freezes entrance alpha");
        CHECK(!memcmp(&saved,&stage.stats,sizeof(saved)),"pause/same tick freezes event counters");
    }
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,0);
    garden_stage_set_enabled(&stage,0);
    CHECK(garden_stage_boss_alpha(&stage,&game,1)==32,"off during entrance is opaque");
    for(unsigned b=0;b<2;b++) {
        cache_step(&stage,&game,b,1);
        CHECK(!memcmp(static_pixels,cached_pixels[b],sizeof(pixels)),"off during warning restores original footer");
    }
    garden_stage_set_enabled(&stage,7);
    CHECK(stage.enabled==1U && garden_stage_boss_alpha(&stage,&game,1)==alpha,
          "on retains current entrance epoch instead of replaying fade");
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    game.paused=0; game.tick++; garden_stage_observe(&stage,&game);
    CHECK(garden_stage_boss_alpha(&stage,&game,1)>=alpha,"resumed next logic tick continues fade");
    game.mode=GARDEN_MODE_GAMEOVER; game.boss.active=0; game.tick++;
    garden_stage_observe(&stage,&game);
    CHECK(garden_stage_boss_alpha(&stage,&game,1)==32,"leaving Boss clears fade");
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,-1);
    game.tick=0; game.mode=GARDEN_MODE_WAVES; garden_stage_observe(&stage,&game);
    CHECK(stage.stats.restarts==1,"tick rollback records one restart");
    CHECK(!stage.boss_entry_valid,"restart clears entrance timestamp");
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,-1);
    garden_stage_reset(&stage); game.mode=GARDEN_MODE_BOSS; game.boss.active=1;
    garden_stage_observe(&stage,&game);
    CHECK(!stage.stats.boss_entries && garden_stage_boss_alpha(&stage,&game,1)==32,
          "first observation already in Boss cannot invent entrance");
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,-1);
    waves(&game,100); start(&stage,&game); garden_stage_set_enabled(&stage,0); enter(&stage,&game);
    game.tick+=101U; garden_stage_observe(&stage,&game);
    garden_stage_set_enabled(&stage,1);
    CHECK(stage.stats.boss_entries==1 && garden_stage_boss_alpha(&stage,&game,1)==32,
          "off observes entry but enabling after expiry does not replay it");
    for(unsigned b=0;b<2;b++) {
        cache_step(&stage,&game,b,-1);
        CHECK(stage.cache[b].kind==GARDEN_STAGE_FOOTER,"expired off-to-on entry has ordinary footer");
    }
    game.mode=GARDEN_MODE_WAVES; garden_stage_observe(&stage,&game);
    CHECK(stage.stats.restarts==1 && !stage.boss_entry_valid,
          "same-tick synthetic return to waves clears entrance once");
    garden_stage_observe(&stage,&game);
    CHECK(stage.stats.restarts==1,"same-tick restart observation remains idempotent");
    puts("PASS: pause freeze/resume, Gameover exit, restart rollback, no fabricated initial-Boss entrance");
}

static void victory_and_cache(void)
{
    GardenStage stage; GardenGame game;
    waves(&game,100); start(&stage,&game);
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    game.tick++; game.mode=GARDEN_MODE_VICTORY; game.score=4294967295U;
    garden_stage_observe(&stage,&game);
    CHECK(stage.stats.victories==1,"victory transition recorded once");
    for(unsigned b=0;b<2;b++) {
        cache_step(&stage,&game,b,1);
        CHECK(stage.cache[b].kind==GARDEN_STAGE_VICTORY,"victory banner active");
    }
    game.score=1U; game.demo_mode=1;
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    game.demo_mode=0; game.score=1320;
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    garden_stage_set_enabled(&stage,0);
    CHECK(garden_stage_boss_alpha(&stage,&game,1)==32,"disabled stage supplies opaque Boss");
    for(unsigned b=0;b<2;b++) {
        cache_step(&stage,&game,b,1);
        CHECK(!memcmp(static_pixels,cached_pixels[b],sizeof(pixels)),"off restores exact original two footer lines");
    }
    garden_stage_set_enabled(&stage,1);
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    garden_stage_ui_reset(&stage);
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    memcpy(pixels,cached_pixels[0],sizeof(pixels)); calls=0;
    CHECK(garden_stage_draw(&stage,&game,2)==1 && calls>0,"unknown framebuffer redraws conservatively");
    for(unsigned b=0;b<2;b++) cache_step(&stage,&game,b,1);
    game.tick=0; game.mode=GARDEN_MODE_WAVES; game.score=0; garden_stage_observe(&stage,&game);
    for(unsigned b=0;b<2;b++) {
        cache_step(&stage,&game,b,1);
        CHECK(!memcmp(static_pixels,cached_pixels[b],sizeof(pixels)),"restart clears victory text and restores exact footer");
    }
    calls=0;
    CHECK(garden_stage_draw(NULL,&game,0)==-1 && garden_stage_draw(&stage,NULL,0)==-1 &&
          calls==0,"null draw arguments produce no pixels");
    puts("PASS: two physical framebuffer caches, maximum-to-low score, demo/manual, off/on/reset, null/invalid index");
}

static void core_noninterference(void)
{
    GardenGame baseline,observed; GardenStage stage;
    garden_init(&baseline,12345U); observed=baseline; start(&stage,&observed);
    unsigned natural_restarts=0, natural_victories=0, paused_steps=0;
    for(unsigned step=0;step<12000U;step++) {
        uint16_t input=(step==1500U || step==1530U) ? GARDEN_INPUT_PAUSE:0;
        uint32_t old_tick=baseline.tick;
        unsigned old_mode=baseline.mode;
        garden_step(&baseline,input); garden_step(&observed,input);
        garden_stage_observe(&stage,&observed);
        if(baseline.tick<old_tick) natural_restarts++;
        if(old_mode!=GARDEN_MODE_VICTORY && baseline.mode==GARDEN_MODE_VICTORY) natural_victories++;
        if(baseline.paused) paused_steps++;
        (void)garden_stage_boss_alpha(&stage,&observed,step&1U);
        if(step%13U==0) (void)garden_stage_draw(&stage,&observed,(step/13U)&1U);
        CHECK(!memcmp(&baseline,&observed,sizeof(baseline)),
              "production stage observe/alpha/draw preserves complete actual core snapshots");
    }
    CHECK(natural_restarts>=2U && natural_victories>=2U,"actual seed12345 reaches repeated victories and automatic restarts");
    CHECK(paused_steps==30U,"real paused steps exercised");
    CHECK(stage.stats.restarts==natural_restarts && stage.stats.victories==natural_victories,
          "event counts match actual core transitions across automatic restart");
    CHECK(stage.stats.boss_entries>=natural_victories,"actual WAVES-to-BOSS entrances observed");
    printf("PASS:12000 real-core snapshots unchanged, actual victories=%u automatic_restarts=%u pause_steps=%u\n",
           natural_victories,natural_restarts,paused_steps);
}

static void render_failure_retries(void)
{
    for(unsigned failure=0;failure<6U;failure++) {
        GardenStage stage; GardenGame game;
        static_frame(); waves(&game,100); start(&stage,&game);
        for(unsigned b=0;b<2U;b++) cache_step(&stage,&game,b,1);
        enter(&stage,&game);
        memcpy(pixels,cached_pixels[0],sizeof(pixels));
        box_seen=glyph_seen=injected=0;
        fail_field=failure%3U;
        fail_box_at=failure<3U ? 3U:0U;
        fail_glyph_at=failure>=3U ? 5U:0U;
        CHECK(garden_stage_draw(&stage,&game,0)==-2,
              "mid-draw renderer error rejects cache commit with retry result");
        CHECK(injected==1U && !fail_box_at && !fail_glyph_at,"one simulated box/font failure actually injected");
        CHECK(!stage.cache[0].valid && !stage.cache[1].valid,"renderer failure invalidates both physical framebuffer caches");
        memcpy(cached_pixels[0],pixels,sizeof(pixels));
        for(unsigned b=0;b<2U;b++) cache_step(&stage,&game,b,1);
        CHECK(stage.cache[0].valid && stage.cache[1].valid,
              "same presentation fully retries and caches only after clean renderer result");
    }
    puts("PASS: six mid-draw box/font errors/bad_targets/atlas_bounds failures invalidate both caches, retry full RGB565 oracle");
}

int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"Usage:test_stage ATLAS\n"); return 2; }
    FILE *in=fopen(argv[1],"rb"); CHECK(in!=NULL,"open accepted FX atlas");
    CHECK(fread(atlas,1,sizeof(atlas),in)==sizeof(atlas),"read complete font atlas");
    CHECK(fgetc(in)==EOF,"no appended atlas data"); fclose(in);
    static_frame(); entrance_timing();
    static_frame(); pause_lifecycle();
    static_frame(); victory_and_cache();
    static_frame(); core_noninterference();
    render_failure_retries();
    printf("PASS: garden_stage host contract checks=%u; host-only, no board acceptance or measured performance claim\n",checks);
    return 0;
}
