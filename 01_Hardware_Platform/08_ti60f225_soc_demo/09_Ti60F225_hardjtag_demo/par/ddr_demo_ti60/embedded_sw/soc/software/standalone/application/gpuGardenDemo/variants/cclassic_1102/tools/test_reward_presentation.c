/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host RGB565/cache and bounded presentation contracts. No MMIO or UART. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "garden_stage.h"
#include "garden_animation.h"
#include "garden_assets.h"
#include "garden_ui.h"

enum { W=1280,H=720 };
static uint16_t image[H][W],buffers[2][H][W],reference[H][W],base[H][W];
static unsigned char atlas[GARDEN_ATLAS_BYTES];
static GardenRenderStats renderer;
static unsigned checks,calls,field_calls,full_clears,fail_next,inside_only;
#define CHECK(c,m) do { ++checks;if(!(c)) { fprintf(stderr,"FAIL %u: %s\n",__LINE__,m);exit(2); } } while(0)
const GardenRenderStats *garden_render_stats(void) { return &renderer; }
static void bound(int x,int y,int w,int h)
{
    CHECK(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=W && y+h<=H,"physical render bounds");
    CHECK((x>=320 && x+w<=960 && y>=120 && y+h<=600) ||
          (!inside_only && x>=504 && x+w<=948 && y>=634 && y+h<=678),"only field or owned footer");
}
void garden_render_box(int x,int y,int w,int h,uint16_t color)
{
    bound(x,y,w,h);calls++;field_calls+=y<600;
    full_clears+=(x==320 && y==120 && w==640 && h==480);
    if(fail_next) { renderer.errors++;fail_next=0;return; }
    for(int row=y;row<y+h;row++) for(int col=x;col<x+w;col++) image[row][col]=color;
}
void garden_render_text_style(int x,int y,const char *text,unsigned style)
{
    unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE:
                  style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:
                  style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE:GARDEN_ASSET_FONT_WHITE_BASE;
    int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16:8;
    calls++;
    while(*text) {
        int id=garden_glyph(*text++);CHECK(id>=0,"all result glyphs supported");
        const garden_sprite_t *s=garden_asset(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE);
        CHECK(s!=NULL,"actual atlas glyph exists");
        int left=x-s->anchor_x,top=y-s->anchor_y;bound(left,top,s->width,s->height);
        if(fail_next) { renderer.atlas_bounds++;fail_next=0; }
        else for(unsigned row=0;row<s->height;row++) for(unsigned col=0;col<s->width;col++) {
            unsigned off=s->offset+row*s->stride+2U*col;
            CHECK(off+1U<sizeof(atlas),"glyph source inside atlas");
            uint16_t value=(uint16_t)(atlas[off]|((unsigned)atlas[off+1U]<<8U));
            if(value!=GARDEN_KEY_COLOR) image[top+(int)row][left+(int)col]=value;
        }
        x+=advance;
    }
}
void garden_render_text(int x,int y,const char *s)
{ garden_render_text_style(x,y,s,GARDEN_TEXT_SMALL_WHITE); }

static void observe(GardenStage *s,GardenGame *g)
{ GardenGame copy=*g;garden_stage_observe(s,g);CHECK(!memcmp(&copy,g,sizeof(copy)),"stage observation read-only"); }
static void setup(GardenStage *s,GardenGame *g)
{
    memset(s,0,sizeof(*s));memset(g,0,sizeof(*g));garden_stage_reset(s);
    g->tick=100;g->mode=GARDEN_MODE_BOSS;g->boss.active=1;g->boss.x=320000;g->boss.y=100000;
    g->score=950;g->demo_mode=1;g->reward.high_score=2959;g->reward.run_ticks=3600;
    g->reward.hits_taken=2;g->reward.graze_count=38;g->reward.items_collected=4;g->reward.bombs_used=1;
    observe(s,g);g->tick++;g->mode=GARDEN_MODE_VICTORY;g->boss.active=0;g->score=1959;observe(s,g);
}
static void reset_pixels(void)
{
    for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) image[y][x]=(uint16_t)(0x0841U+(x&3U));
    memcpy(base,image,sizeof(image));memcpy(buffers[0],image,sizeof(image));memcpy(buffers[1],image,sizeof(image));
}
static void pixel_step(GardenStage *s,GardenGame *g,unsigned buffer)
{
    GardenGame copy=*g;memcpy(image,buffers[buffer],sizeof(image));
    CHECK(garden_stage_prepare_frame(s,g,buffer)>=0,"prepare writable target");
    CHECK(garden_stage_draw(s,g,buffer)>=0,"cached stage draw");
    memcpy(buffers[buffer],image,sizeof(image));
    calls=0;CHECK(garden_stage_draw(s,g,buffer)==0 && calls==0,"unchanged FB emits zero commands");
    GardenStage fresh=*s;garden_stage_ui_reset(&fresh);memcpy(image,base,sizeof(image));
    CHECK(garden_stage_draw(&fresh,g,buffer)==1,"fresh full oracle redraw");
    memcpy(reference,image,sizeof(image));
    CHECK(!memcmp(reference,buffers[buffer],sizeof(image)),"cached RGB565 equals full actual-font redraw for whole framebuffer");
    CHECK(!memcmp(&copy,g,sizeof(copy)),"complete game unchanged by prepare/draw");
}
static void stage_contracts(void)
{
    GardenStage s;GardenGame g;setup(&s,&g);reset_pixels();
    CHECK(!garden_stage_modal_active(&s,&g),"death begins before modal");
    for(unsigned age=0;age<100;age++) {
        g.tick=s.victory_tick+age;observe(&s,&g);
        CHECK(garden_stage_visible_score(&s,&g)==950,"left HUD retains prekill score duringdeath");
        struct { GardenDraw d[32];uint32_t canary; } a;
        memset(&a,0,sizeof(a));a.canary=0xface1234U;
        a.d[0]=(GardenDraw){17,18,GARDEN_ASSET_SHOT,32};
        GardenGame before=g;unsigned n=garden_stage_append_effects(&s,&g,a.d,1,32,1);
        CHECK(n>1 && n<=1+GARDEN_STAGE_DEATH_DRAW_MAX,"all100ticks have bounded death presentation");
        CHECK(a.d[0].x==17 && a.d[0].asset==GARDEN_ASSET_SHOT,"prior real objects retained");
        for(unsigned i=1;i<n;i++) CHECK(garden_asset(a.d[i].asset)!=NULL && a.d[i].alpha>0 && a.d[i].alpha<=32,"only known atlas assets/alpha");
        CHECK(a.canary==0xface1234U && !memcmp(&before,&g,sizeof(g)),"death effects no core mutation or overflow");
        for(unsigned cap=1;cap<14;cap++) {
            GardenDraw d[16];memset(d,0,sizeof(d));d[cap]=(GardenDraw){-123,456,0xbeefU,31};
            unsigned kept=garden_stage_append_effects(&s,&g,d,1,cap,0);
            CHECK(kept<=cap && d[cap].asset==0xbeefU,"tight capacity exact guard preserved");
            for(unsigned i=1;i<kept;i++) CHECK(d[i].alpha==32,"alpha-off opaque bounded fallback");
        }
    }
    unsigned ages[]={100,101,102,110,140,179,180,219,220,250,319};
    for(unsigned i=0;i<sizeof(ages)/sizeof(*ages);i++) {
        g.tick=s.victory_tick+ages[i];observe(&s,&g);
        CHECK(garden_stage_modal_active(&s,&g),"modal starts only after100ticks");
        if(i) full_clears=0;
        for(unsigned b=0;b<2;b++) pixel_step(&s,&g,b);
        /* pixel_step also full-redraws reference; inspect retained logical stats. */
        CHECK(s.stats.modal_redraws==2U,"one static modal build per physical FB");
        CHECK(s.cache[0].score==garden_stage_visible_score(&s,&g),"modal andHUD share one visiblecredit");
        if(ages[i]>=180U) CHECK(s.cache[0].score==g.score && s.cache[1].score==g.score,"score reaches exact final credit after2s");
        if(ages[i]<220U) CHECK(!s.cache[0].progress,"restart hint withheld for3s aftercard");
        else CHECK(s.cache[0].progress,"restart hint after3s");
    }
    GardenStage paused=s;observe(&s,&g);CHECK(!memcmp(&paused,&s,sizeof(s)),"same tick observer idempotent");
    g.tick=0;g.mode=GARDEN_MODE_WAVES;g.score=0;observe(&s,&g);
    CHECK(!garden_stage_modal_active(&s,&g),"restart exits card");
    for(unsigned b=0;b<2;b++) {
        memcpy(image,buffers[b],sizeof(image));inside_only=1;
        CHECK(garden_stage_prepare_frame(&s,&g,b)==1,"each old card cleared before fresh scene");
        CHECK(garden_stage_prepare_frame(&s,&g,b)==0,"no redundant playfield clears");
        inside_only=0;garden_render_box(620,350,6,6,0xabcdU);
        CHECK(garden_stage_draw(&s,&g,b)==1,"restart footer restored");
        CHECK(image[350][620]==0xabcdU,"stage draw never erases fresh gameplay object");
        for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) if(!(x>=320 && x<960 && y>=120 && y<600) && !(x>=504 && x<948 && y>=634 && y<678))
            CHECK(image[y][x]==base[y][x],"all UI borders/panels outsideownedregions unchanged");
    }
    setup(&s,&g);g.score=UINT32_MAX;g.tick=s.victory_tick+140;observe(&s,&g);
    for(unsigned b=0;b<2;b++) CHECK(garden_stage_draw(&s,&g,b)==1,"large score first draw");
    CHECK(s.cache[0].score==950U+(UINT32_MAX-950U)/2U,"large score roll avoids overflow");
    garden_stage_ui_reset(&s);fail_next=1;
    CHECK(garden_stage_draw(&s,&g,0)==-2 && !s.cache[0].valid && !s.cache[1].valid,"partial error rejects both histories");
    CHECK(garden_stage_draw(&s,&g,0)==1 && s.cache[0].valid,"failed card fully retried");
    garden_stage_set_enabled(&s,0);fail_next=1;
    CHECK(garden_stage_prepare_frame(&s,&g,0)==-2 && (s.modal_buffers&1U),"failed restart cleanup retains need-to-clear mark");
    CHECK(garden_stage_prepare_frame(&s,&g,0)==1 && !(s.modal_buffers&1U),"cleanup retry succeeds before fresh scene");
    CHECK(garden_stage_prepare_frame(&s,&g,1)==1 && !s.modal_buffers,"disable clears both physical cards");
    garden_stage_set_enabled(&s,1);
    CHECK(garden_stage_draw(&s,&g,0)==1 && (s.modal_buffers&1U),"reenable atsameepoch rebuildscard");
    g.tick++;g.score=3000;g.reward.high_score=3000;g.reward.items_collected++;
    observe(&s,&g);
    CHECK(!s.cache[0].valid && !s.cache[1].valid,"real latepickup invalidatesstaticBESTandPICKUPS onbothFBs");
    for(unsigned b=0;b<2;b++) pixel_step(&s,&g,b);
    GardenDraw clipped[2]={{1,2,GARDEN_ASSET_SHOT,32},{2,3,GARDEN_ASSET_SHOT,32}};
    g.tick=s.victory_tick+50;observe(&s,&g);
    CHECK(garden_stage_append_effects(&s,&g,clipped,2,1,1)==1,"malformed incoming count clamps without wrap");
    CHECK(garden_stage_draw(NULL,&g,0)==-1 && garden_stage_prepare_frame(&s,NULL,0)==-1,"null guard");
    printf("PASS stage:100tick layered death,two-FB RGB565 oracle,rolling score,prompt,restart clear,error retry\n");
}
static void animation_contracts(void)
{
    GardenGame g={0};GardenAnimation a;garden_animation_reset(&a);
    g.tick=1;g.mode=GARDEN_MODE_WAVES;g.lives=3;g.player.active=1;g.player.x=320000;g.player.y=350000;
    garden_animation_observe(&a,&g);g.tick++;g.lives=2;g.player.respawning=50;
    garden_animation_observe(&a,&g);
    CHECK(a.death_active && a.stats.death_triggers==1,"livesloss real death event");
    for(unsigned age=0;age<40;age++) {
        g.tick=2+age;garden_animation_observe(&a,&g);garden_animation_set_fraction(&a,128);
        GardenDraw d[16]={{100,120,GARDEN_ASSET_DART_BASE,32},{320,350,GARDEN_ASSET_FOCUS_GRACE,32}};
        GardenGame before=g;unsigned n=garden_animation_apply(&a,&g,d,2,16,1);
        CHECK(n==10 && a.stats.fragments==8,"eight bright fragments for1s");
        CHECK(d[9].asset==GARDEN_ASSET_FOCUS_GRACE,"real protection marker remainslast");
        for(unsigned i=1;i<9;i++) CHECK(d[i].asset==GARDEN_ASSET_HIT_SPARK && d[i].alpha>0,"white/gold debris not fullplaneghost");
        CHECK(!memcmp(&before,&g,sizeof(g)),"animation completecore immutable");
    }
    g.tick++;garden_animation_observe(&a,&g);CHECK(!a.death_active,"fragments expire exactly40ticks");
    g.player.respawning=0;g.player.grace=40;g.reward.bomb_flash_ticks=12;
    GardenDraw d[16]={{100,120,GARDEN_ASSET_DART_BASE,32},{320,350,GARDEN_ASSET_FOCUS_GRACE,32}};
    unsigned n=garden_animation_apply(&a,&g,d,2,16,1);
    CHECK(n==10 && d[9].asset==GARDEN_ASSET_FOCUS_GRACE,"real bomb adds8localrings under focusmarker");
    CHECK(d[0].asset==GARDEN_ASSET_DART_BASE && d[0].x==100 && d[0].y==120,"bomb animation neverchangesdangercolour/position");
    for(unsigned cap=2;cap<10;cap++) {
        memset(d,0,sizeof(d));d[0]=(GardenDraw){100,120,GARDEN_ASSET_DART_BASE,32};d[cap]=(GardenDraw){123,456,0xbeefU,32};
        CHECK(garden_animation_apply(&a,&g,d,2,cap,0)==cap && d[cap].asset==0xbeefU,"bomb capacity guard andoriginalcontent");
    }
    printf("PASS animation:real death trigger,8bright fragments1s,gracepulse,12tick realbomb bounded8rings\n");
}
int main(int argc,char **argv)
{
    FILE *f;
    if(argc!=2) return 2;
    f=fopen(argv[1],"rb");CHECK(f!=NULL,"openactualatlas");
    CHECK(fread(atlas,1,sizeof(atlas),f)==sizeof(atlas) && fgetc(f)==EOF,"exact atlas length");fclose(f);
    stage_contracts();animation_contracts();
    printf("PASS reward_presentation checks=%u stage_bytes=%u animation_bytes=%u\n",checks,(unsigned)sizeof(GardenStage),(unsigned)sizeof(GardenAnimation));return 0;
}
