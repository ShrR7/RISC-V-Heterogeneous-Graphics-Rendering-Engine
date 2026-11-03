/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure core snapshots, frozen atlas safety and actual UI call contracts.
 * No hardware clocks, GPU, UART or framebuffer screenshots are claimed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "garden_animation.h"
#include "garden_assets.h"
#include "garden_ui.h"
static unsigned checks;
#define CHECK(c,m) do { checks++;if(!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m);exit(2); } } while(0)
typedef struct { int x,y,w,h;uint16_t color; } Box;
typedef struct { int x,y;unsigned style;char text[80]; } Text;
static Box boxes[4096];static Text texts[1024];static unsigned nb,nt;
void garden_render_box(int x,int y,int w,int h,uint16_t color)
{ CHECK(nb<4096,"UI mock storage bounded");boxes[nb++]=(Box){x,y,w,h,color}; }
void garden_render_text_style(int x,int y,const char *text,unsigned style)
{ CHECK(nt<1024,"UI text mock storage bounded");texts[nt]=(Text){x,y,style,{0}};snprintf(texts[nt++].text,80,"%s",text); }
void garden_render_text(int x,int y,const char *text)
{ garden_render_text_style(x,y,text,GARDEN_TEXT_SMALL_WHITE); }
static void reset_calls(void) { nb=nt=0; }
static unsigned text_at(int x,int y,const char *value,unsigned style)
{ for(unsigned i=0;i<nt;i++) if(texts[i].x==x && texts[i].y==y && texts[i].style==style && !strcmp(texts[i].text,value)) return 1;return 0; }
static GardenGame game,saved;
static void observe(GardenAnimation *a)
{ saved=game;garden_animation_observe(a,&game);CHECK(!memcmp(&saved,&game,sizeof(game)),"presentation observation never writes any core byte"); }
static unsigned apply(GardenAnimation *a,GardenDraw *d,unsigned n,unsigned capacity,unsigned alpha)
{ saved=game;n=garden_animation_apply(a,&game,d,n,capacity,alpha);CHECK(!memcmp(&saved,&game,sizeof(game)),"palette/camera/debris never writes HP,clock,RNG,collisions or true bulletcolour");return n; }
static void palettes(const unsigned char *atlas)
{
    GardenAnimation a;garden_animation_reset(&a);garden_init(&game,12345);observe(&a);
    GardenDraw d[82],original[82];unsigned count=0;
    uint32_t palette_hash[2][5][8];
    for(unsigned type=1;type<=2;type++) for(unsigned color=1;color<=5;color++) for(unsigned rotation=0;rotation<8;rotation++) {
        unsigned id=garden_bullet_id(type,color,rotation*128U);
        const garden_sprite_t *old=garden_asset(id);
        CHECK(old && old->width && old->height && old->stride>=old->width*2U &&
              old->offset+(old->height-1U)*old->stride+old->width*2U<=GARDEN_ATLAS_BYTES,"all80 original family/rotation assets retain valid frozen source regions");
        unsigned visible_pixels=0;uint32_t hash=2166136261U;
        for(unsigned y=0;y<old->height;y++) for(unsigned x=0;x<old->width;x++) {
            unsigned old_at=old->offset+y*old->stride+x*2U;
            uint16_t before=(uint16_t)(atlas[old_at]|atlas[old_at+1U]<<8);
            visible_pixels+=before!=GARDEN_KEY_COLOR;
            hash=(hash^before)*16777619U;
        }
        CHECK(visible_pixels>0,"every original enemy family/rotation has visible non-Key pixels");
        palette_hash[type-1U][color-1U][rotation]=hash;
        d[count++]=(GardenDraw){(int16_t)(10+rotation),(int16_t)(30+color),(uint16_t)id,(uint8_t)(1U+(color*3U+rotation)%32U)};
    }
    for(unsigned type=0;type<2;type++) for(unsigned rotation=0;rotation<8;rotation++)
        for(unsigned color=0;color<5;color++) for(unsigned other=0;other<color;other++)
            CHECK(palette_hash[type][color][rotation]!=palette_hash[type][other][rotation],"all five original visible palettes stay distinct at each enemy type/rotation");
    d[count++]=(GardenDraw){10,400,GARDEN_ASSET_SHOT,32};d[count++]=(GardenDraw){10,400,GARDEN_ASSET_FOCUS_VULNERABLE,32};
    memcpy(original,d,sizeof(d));
    for(unsigned alpha=0;alpha<2;alpha++) {
        memcpy(d,original,sizeof(d));CHECK(apply(&a,d,count,82,alpha)==count,"original palette adds no sprite or draw cost in either Alpha mode");
        CHECK(!memcmp(d,original,sizeof(d)),"all80 enemy IDs/angle/coordinates/opacity plus friendly shot/real marker are byte-identical without death camera");
        for(unsigned i=0;i<80;i++) {
            unsigned type=i/40U+1U,color=(i%40U)/8U+1U,rotation=i%8U;
            const garden_sprite_t *selected=garden_asset(d[i].asset),*expected=garden_asset(garden_bullet_id(type,color,rotation*128U));
            CHECK(selected==expected,"each original colour/type/rotation selects the exact original atlas descriptor");
            for(unsigned y=0;y<expected->height;y++) for(unsigned x=0;x<expected->width;x++) {
                unsigned src=selected->offset+y*selected->stride+x*2U,ref=expected->offset+y*expected->stride+x*2U;
                CHECK(atlas[src]==atlas[ref] && atlas[src+1U]==atlas[ref+1U],"full original non-Key and Key source pixels preserved, not merely a matching collision silhouette");
            }
        }
        CHECK(a.stats.enemy_bullets==80U && !a.stats.remapped_total && !a.stats.fragments && !a.stats.camera_x && !a.stats.camera_y,"original palette counter evidence has zero remaps and no phantom death/shake");
    }
    CHECK(a.stats.danger_bullet_frames==2U,"enemy bullet presence is still counted per complete draw build");
    puts("PASS:80actual frozen enemy sprites: five original palettes, exact source ID/pixels/geometry/angle/opacity and unchanged core colour");
}
static void death(void)
{
    GardenAnimation a;garden_animation_reset(&a);garden_init(&game,12345);game.demo_mode=0;game.player.grace=0;
    game.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=2,.ttl=100,.cord_wait=-1,.x=game.player.x,.y=game.player.y};
    game.effects[0]=(GardenEffect){.active=1,.age=0,.big=1,.x=game.player.x-30000,.y=game.player.y};
    observe(&a);int32_t death_x=game.player.x,death_y=game.player.y;
    garden_step(&game,0);observe(&a);
    CHECK(a.death_active && a.stats.death_triggers==1U && game.lives==2U && game.player.respawning==50U,"only a genuine real-core vulnerable death starts the presentation; original respawn length retained");
    CHECK(a.death_x==death_x/GARDEN_GRAIN && a.death_y==death_y/GARDEN_GRAIN,"real fresh death effect anchors fragments, not the teleported respawn position");
    GardenAnimation same=a;observe(&a);CHECK(!memcmp(&same,&a,sizeof(a)),"same tick cannot replay the death or consume lifetime");
    GardenDraw base[5]={{320,400,0x2008U,32},{320,400,GARDEN_ASSET_FOCUS_GRACE,32},
                        {100,100,GARDEN_ASSET_DART_BASE+8,32},{100,60,0x4026U,32},{100,40,0x805FU,16}};
    GardenDraw d[12],paused[12];memcpy(d,base,sizeof(base));garden_animation_set_fraction(&a,0);
    unsigned n=apply(&a,d,5,12,1);CHECK(n==11 && a.stats.fragments==6U,"six bounded small fragments only use free admission slots");
    CHECK(abs(a.stats.camera_x)<=4 && abs(a.stats.camera_y)<=4 && (a.stats.camera_x || a.stats.camera_y),"only protected death begins bounded4px microcamera");
    for(unsigned i=0;i<5U;i++) {
        unsigned index=i<1U ? i:i+6U;
        CHECK(d[index].x==base[i].x+a.stats.camera_x && d[index].y==base[i].y+a.stats.camera_y,"player, actual marker, enemybullet, HPbar and damage text all receive exactly the same world offset");
        CHECK(d[index].asset==base[i].asset && d[index].alpha==base[i].alpha,"protected death camera retains original enemy colour/angle, actor/bar/text asset and opacity");
    }
    CHECK(d[7].asset==GARDEN_ASSET_FOCUS_GRACE,"debris inserts before the existing marker, preserves high-priority object's later paint order");
    for(unsigned i=1;i<7U;i++) CHECK(d[i].asset==GARDEN_ASSET_SHOT || d[i].asset==GARDEN_ASSET_HIT_SPARK,"no new atlas image/full-plane ghost or big Alpha circle is introduced");
    GardenDraw canary={-123,456,0x9999U,3};memcpy(d,base,sizeof(base));d[5]=canary;
    CHECK(apply(&a,d,5,5,1)==5 && !memcmp(&canary,&d[5],sizeof(canary)) && a.stats.fragment_dropped==6U,"no free slots: real content retained, fragment cut counted, capacity canary safe");
    game.player.respawning=0;game.player.grace=0;memcpy(d,base,sizeof(base));apply(&a,d,5,12,1);
    CHECK(!a.stats.camera_x && !a.stats.camera_y,"even an active death envelope cannot shake a now-vulnerable player/hazards");
    game.player.respawning=50;game.player.grace=180;
    unsigned old_alpha=32U;int old_x=-10000;
    for(unsigned age=1;age<30;age++) {
        game.tick++;observe(&a);garden_animation_set_fraction(&a,192U);
        memcpy(d,base,sizeof(base));n=apply(&a,d,5,12,1);
        CHECK(n==11 && a.death_age==age && a.stats.fragments==6U,"death lasts exactly30logic ticks, independent of render frequency");
        CHECK(d[1].alpha<=old_alpha && d[1].alpha>0U,"small particle opacity decays monotonically in Alpha mode");old_alpha=d[1].alpha;
        if(age>=4U) CHECK(!a.stats.camera_x && !a.stats.camera_y,"camera returns to zero after100ms despite remaining750ms fragments");
        if(age>4U) CHECK(d[1].x<=old_x,"negative-direction fragment moves away from death point instead of rigid original sprite");
        old_x=d[1].x;
        if(age==10U) {
            game.paused=1;observe(&a);same=a;garden_animation_set_fraction(&a,1U);
            CHECK(a.fraction_q8==same.fraction_q8,"paused sampling freezes the visual subtick fraction too");
            memcpy(paused,base,sizeof(base));unsigned p=apply(&a,paused,5,12,1);
            memcpy(d,base,sizeof(base));unsigned q=apply(&a,d,5,12,1);
            CHECK(p==q && !memcmp(d,paused,p*sizeof(*d)),"repeated paused frames have byte-identical debris/camera output");
            game.paused=0;observe(&a);
        }
    }
    game.tick++;observe(&a);memcpy(d,base,sizeof(base));CHECK(apply(&a,d,5,12,1)==5 && !a.death_active && !a.stats.fragments,"all death fragments disappear at750ms with no ad hoc core restart");
    game.tick++;game.lives--;game.player.grace=180;observe(&a);CHECK(a.death_active && a.stats.death_triggers==2U,"later death starts an independent envelope");
    memcpy(d,base,sizeof(base));apply(&a,d,5,12,0);for(unsigned i=1;i<7U;i++) CHECK(d[i].alpha==32U,"Alpha-off fallback stays opaque until normal fixed lifetime expiry");
    garden_step(&game,GARDEN_INPUT_RESTART);observe(&a);memcpy(d,base,sizeof(base));CHECK(apply(&a,d,5,12,1)==5 && !a.death_active && !a.fraction_q8,"restart clears camera and all particle state, preserves original lives/clock rules");
    a.death_active=1;a.death_age=0;game.player.grace=1;GardenDraw extremes[2]={{INT16_MAX,INT16_MIN,GARDEN_ASSET_SHOT,32},{0,0,GARDEN_ASSET_DART_BASE,32}};
    apply(&a,extremes,2,2,1);CHECK(extremes[0].x==INT16_MAX,"malformed extreme coordinates saturate rather than wrap into view");
    CHECK(garden_animation_stats(&a)==&a.stats && garden_animation_stats(NULL)==NULL,"stats/null API safe");
    garden_animation_set_fraction(&a,UINT_MAX);CHECK(a.fraction_q8==255U,"malformed subtick sample clamped");
    puts("PASS: genuine death anchor/750ms tiny debris, uniform protected camera, lifetime/fraction/pause/restart, priority/capacity/null/overflow guards");
}
static void ui(void)
{
    GardenHud h={.state="STAGE1BOSS",.score=1234,.lives=2,.bullets=128,.peak=128,.fps10=598,
        .gpu_bench_us=10000,.cpu_bench_us=43210,.errors=7,.alpha_supported=1,.alpha_enabled=1,.demo=1};
    reset_calls();garden_ui_static();unsigned compact=0,bright_fill=0;
    for(unsigned i=0;i<nb;i++) {
        const Box *b=&boxes[i];
        if(b->x==32 && b->y==372 && b->w==256 && b->h==176) compact++;
        bright_fill+=b->color==GARDEN_UI_MAGENTA || b->color==GARDEN_UI_GOLD;
    }
    CHECK(compact==1U && !bright_fill,"static cockpit has compact176px performance panel and no competing warm neon frame fills");
    CHECK(text_at(536,44,"GARDEN OF COLOURED LIGHTS",GARDEN_TEXT_SMALL_CYAN),"title reduces to existing8pxglyphs instead of another huge bright16pxbanner");
    for(unsigned i=0;i<nt;i++) CHECK(texts[i].style<GARDEN_TEXT_LARGE_WHITE,"all static decorative headings are small; dynamic real score can remain legible large");
    reset_calls();garden_ui_hud(&h);
    CHECK(text_at(176,432,"59.8",GARDEN_TEXT_SMALL_WHITE),"FPS stays honest decimal sample, never a fixed60label");
    CHECK(text_at(176,450,"10000",GARDEN_TEXT_SMALL_WHITE) && text_at(176,468,"43210",GARDEN_TEXT_SMALL_WHITE) &&
          text_at(176,486,"4.32X",GARDEN_TEXT_SMALL_WHITE) && text_at(176,516,"7",GARDEN_TEXT_SMALL_WHITE),"real GPU/CPU benchmark, true ratio and errors remain readable in compact HUD");
    CHECK(text_at(56,220,"00001234",GARDEN_TEXT_LARGE_WHITE),"actual score retains original independent large readable window");
    for(unsigned i=0;i<nb;i++) CHECK(!(boxes[i].y==528 && boxes[i].h==4),"ten bright staticFPSgauge cells removed");
    garden_ui_hud_reset();
    for(unsigned buffer=0;buffer<2U;buffer++) {
        reset_calls();garden_ui_hud_cached(&h,buffer);CHECK(nb && nt,"each physical FB initializes own compact truthful values");
        reset_calls();garden_ui_hud_cached(&h,buffer);CHECK(!nb && !nt,"unchanged HUD produces no extra fills/Alpha work");
    }
    h.fps10=601;reset_calls();garden_ui_hud_cached(&h,0);CHECK(text_at(176,432,"60.1",GARDEN_TEXT_SMALL_WHITE),"changed true FPS updates only its known window");
    reset_calls();garden_ui_hud_cached(&h,1);CHECK(text_at(176,432,"60.1",GARDEN_TEXT_SMALL_WHITE),"second physical FB refreshes its independent FPS history");
    h.render_us=99999;h.logic_us=999;reset_calls();garden_ui_hud_cached(&h,0);CHECK(!nb && !nt,"removed decorative timing rows emit no commands; values remain available to logger");
    h.score++;reset_calls();garden_ui_score_cached(&h,0);CHECK(text_at(56,220,"00001235",GARDEN_TEXT_LARGE_WHITE),"score remains an independent high-priority real update");
    puts("PASS: muted static8pxheadings/frames; compact truthful FPS, CPU/GPUratio/errors; unchanged two-FB cached score/HUD contracts");
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL,"frozen atlas opens");
    unsigned char *atlas=(unsigned char *)malloc(GARDEN_ATLAS_BYTES);CHECK(atlas!=NULL,"host-only atlas buffer");
    CHECK(fread(atlas,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES && fgetc(f)==EOF,"exact unchanged atlas source length");fclose(f);
    palettes(atlas);death();ui();free(atlas);printf("PASS: animation/UI host contract checks=%u\n",checks);return 0;
}
