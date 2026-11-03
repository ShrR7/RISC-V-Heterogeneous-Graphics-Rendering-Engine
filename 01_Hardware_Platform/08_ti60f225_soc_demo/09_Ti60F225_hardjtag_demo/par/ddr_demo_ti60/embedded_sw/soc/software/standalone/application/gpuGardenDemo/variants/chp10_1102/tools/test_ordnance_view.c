/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real production renderer/view host contracts; no board or timing claims. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "garden_ordnance_view.h"
#include "garden_assets.h"
#include "garden_ui.h"
#include "garden_combat.h"
#include "garden_animation.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"
static unsigned checks,sel;
static uint16_t base[720][1280],expected[720][1280],other[720][1280];
#define CHECK(c,m) do { checks++;if(!(c)) { fprintf(stderr,"FAIL %u: %s\n",__LINE__,m);exit(2); } } while(0)
static uint16_t *fb(unsigned b) { return (uint16_t *)(uintptr_t)(b ? FB1_ADDR:FB0_ADDR); }
uint32_t display_back_sel(void) { return sel; }
uint32_t display_back_addr(void) { return sel ? FB1_ADDR:FB0_ADDR; }
int display_check_render_target(uint32_t p)
{ return p>=display_back_addr() && p<display_back_addr()+FB_SIZE_BYTES ? GPU_OK:GPU_ESTATE; }
void garden_render_host_fence(void) {}
void data_cache_invalidate_all(void) {}
static void bounds(uint32_t p,unsigned w,unsigned h,unsigned stride)
{
    unsigned off=p-display_back_addr(),x=(off%DISPLAY_STRIDE_BYTES)/2U,y=off/DISPLAY_STRIDE_BYTES;
    CHECK(stride==DISPLAY_STRIDE_BYTES && x+w<=DISPLAY_WIDTH && y+h<=DISPLAY_HEIGHT && display_check_render_target(p)==GPU_OK,"host commands only writable FB andbounds");
    CHECK(w>128 ? h<=16:(h<=64 && w*h<=4096),"unchanged command safety limits");
}
int gpu_fill(uint32_t p,uint32_t w,uint32_t h,uint32_t stride,uint16_t color)
{
    bounds(p,w,h,stride);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) *(uint16_t *)(uintptr_t)(p+y*stride+x*2U)=color;
    return GPU_OK;
}
static int blit(uint32_t src,uint32_t dst,unsigned w,unsigned h,unsigned ss,unsigned ds,uint16_t key,unsigned alpha)
{
    bounds(dst,w,h,ds);
    CHECK(src>=ASSET_BASE_ADDR && (uint64_t)src+(h-1U)*ss+w*2U<=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES && alpha<=32U,"sourceatlas/alpha validated");
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint16_t p=*(uint16_t *)(uintptr_t)(src+y*ss+x*2U),*q=(uint16_t *)(uintptr_t)(dst+y*ds+x*2U);
        if(p!=key) *q=alpha==32U ? p:gpu_alpha_rgb565(p,*q,alpha);
    }
    return GPU_OK;
}
int gpu_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k)
{ return blit(s,d,w,h,ss,ds,k,32U); }
int gpu_alpha_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{ return blit(s,d,w,h,ss,ds,k,a); }
static void initial(void)
{
    garden_render_init();garden_render_alpha_support(1);
    for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"initialize two FBs");
    memcpy(base,fb(0),sizeof(base));sel=0;
}
static void state(GardenGame *g)
{
    memset(g,0,sizeof(*g));g->tick=1000;g->mode=GARDEN_MODE_BOSS;g->boss.active=1;
    g->boss.x=320000;g->boss.y=100000;g->boss.hp=g->boss.max_hp=50000;
    g->laser.x[0]=265000;g->laser.x[1]=375000;g->laser.y=130000;
}
static void geometry(void)
{
    GardenGame g;state(&g);GardenDraw out[3];
    GardenScene scene;garden_scene_reset(&scene);garden_scene_observe(&scene,&g);
    CHECK(garden_ordnance_laser_count(&g)==0,"OFF no phantomlaser");
    unsigned prior=0;
    for(unsigned t=0;t<40;t++) {
        g.laser.phase=GARDEN_LASER_WARN;g.laser.ticks=40-t;
        GardenGame copy=g;CHECK(garden_ordnance_laser_build(&g,out,2,1)==2,"both warning rails");
        CHECK(out[0].alpha>=prior && out[0].alpha>=4 && out[0].alpha<=28,"40tick warn monotonically accumulates");prior=out[0].alpha;
        CHECK(!(out[0].asset&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE)),"warning encodednonactive");
        CHECK(out[0].x==265 && out[1].x==375 && out[0].y==130,"actual collisionx/y worldpivot");
        CHECK(!memcmp(&copy,&g,sizeof(g)),"viewcore completelyreadonly");
        GardenSceneBossPose pose;CHECK(garden_scene_boss_pose(&scene,&g,&pose),"actual Boss pose");
        for(unsigned i=0;i<2;i++) CHECK(pose.wings[i].x==out[i].x && pose.wings[i].y+30==out[i].y,"displayed wingmuzzle matches authoritative beamorigin");
    }
    g.laser.phase=GARDEN_LASER_FIRE;g.laser.ticks=40;
    garden_ordnance_laser_build(&g,out,2,1);
    CHECK((out[0].asset&GARDEN_DRAW_LASER_WIDTH_MASK)==13 && (out[0].asset&GARDEN_DRAW_LASER_ACTIVE) && out[0].alpha==32,"active visiblecore exactly13pixels");
    g.laser.phase=GARDEN_LASER_FADE;prior=32;
    for(unsigned t=0;t<12;t++) {
        g.laser.ticks=12-t;garden_ordnance_laser_build(&g,out,2,1);
        CHECK(out[0].alpha<=prior && (out[0].asset&GARDEN_DRAW_LASER_FADE),"12tick fade monotone");prior=out[0].alpha;
    }
    out[0].asset=0xbeef;CHECK(!garden_ordnance_laser_build(&g,out,1,1) && out[0].asset==0xbeef,"paired laser admission never truncates one side");
}
static void entities(void)
{
    GardenGame g;state(&g);
    g.ordnance=(GardenBomb){.x=320000,.y=210000,.age=5,.active=1};
    g.pickups[0]=(GardenPickup){.x=50000,.y=70000,.active=1,.kind=GARDEN_PICKUP_CORE};
    g.pickups[1]=(GardenPickup){.x=150000,.y=170000,.active=1,.kind=GARDEN_PICKUP_HEAL};
    g.reward.pickup_flash_ticks=16;g.reward.pickup_flash_kind=GARDEN_PICKUP_HEAL;
    g.reward.pickup_flash_x=300000;g.reward.pickup_flash_y=400000;
    GardenDraw original[4]={{320,100,GARDEN_ASSET_BOSS1,32},{320,300,GARDEN_ASSET_SHOT,32},
        {50,350,GARDEN_ASSET_DART_BASE,32},{300,50,GARDEN_DRAW_SCORE_BASE+95,32}};
    for(unsigned blast=0;blast<=16;blast++) {
        g.ordnance.blast_ticks=(uint8_t)blast;GardenGame copy=g;
        unsigned reserve=garden_ordnance_view_reserve(&g);CHECK(reserve==(blast ? 8U:4U),"reservesallrealentitiesplusboundedlocalart");
        for(unsigned capacity=4;capacity<=16;capacity++) {
            GardenDraw out[20];memset(out,0,sizeof(out));memcpy(out,original,sizeof(original));
            out[capacity]=(GardenDraw){-123,456,0xbeef,31};
            unsigned n=garden_ordnance_view_build(&g,out,4,capacity,1),added=n-4;
            CHECK(n<=capacity && out[capacity].asset==0xbeef,"tight drawcapacity canary");
            CHECK(!memcmp(&out[0],&original[0],sizeof(GardenDraw)),"worldbody retainedbefore inserteditems");
            for(unsigned i=1;i<4;i++) CHECK(!memcmp(&out[i+added],&original[i],sizeof(GardenDraw)),"existing projectiles anddamage keeprelativeorder andpaintaboveitems");
            if(added) CHECK(out[1].x==320 && out[1].y==210 && out[1].asset>=GARDEN_ASSET_ORDNANCE_BLAST_BASE,"bomb usesrealworldcenter not doubledanchor");
            if(added>=2) CHECK(out[2].asset==GARDEN_ASSET_ORDNANCE_HEAL && out[2].x==150 && out[2].y==170,"healingentity visible andprioritized");
            CHECK(!memcmp(&copy,&g,sizeof(g)),"world effectsnever change realpickup/bombstate");
        }
    }
    GardenAnimation a;garden_animation_reset(&a);garden_animation_observe(&a,&g);
    GardenDraw out[20];memcpy(out,original,sizeof(original));g.reward.bomb_flash_ticks=12;
    CHECK(garden_animation_apply(&a,&g,out,4,20,1)==4,"old playercentredclearance ring removed");
}
static void reference_beam(const GardenDraw *d,unsigned alpha_on)
{
    unsigned id=GARDEN_ASSET_ORDNANCE_BEAM_BASE+((d->asset&GARDEN_DRAW_LASER_FRAME)!=0);
    const garden_sprite_t *s=garden_asset(id);
    int active=d->asset&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE);
    int cx=320+d->x,top=120+d->y,left=active ? cx-12:cx,right=active ? cx+13:cx+1;
    for(int y=top<120 ? 120:top;y<600;y++) for(int x=left<320 ? 320:left;x<right && x<960;x++) {
        unsigned sy=(unsigned)(y-top)%s->height,sx=(unsigned)(x-cx+12);
        uint16_t p=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+sy*garden_sprite_stride(s)+sx*2U);
        unsigned alpha=alpha_on ? d->alpha:32;
        if(p!=GARDEN_KEY_COLOR) expected[y][x]=alpha==32 ? p:gpu_alpha_rgb565(p,expected[y][x],alpha);
    }
}
static void pixels(void)
{
    GardenGame g;state(&g);initial();unsigned frame=0;
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned alpha=0;alpha<2;alpha++) {
        garden_render_mode((int)cpu);if(garden_render_alpha_enabled()!=(int)alpha)garden_render_alpha_toggle();
        for(unsigned phase=GARDEN_LASER_WARN;phase<=GARDEN_LASER_FADE;phase++) {
            unsigned duration=phase==GARDEN_LASER_FADE ? 12:40;
            for(unsigned age=0;age<duration;age++) {
                g.laser.phase=(uint8_t)phase;g.laser.ticks=(uint16_t)(duration-age);
                g.laser.x[0]=age<duration/2 ? 265000:260000;g.laser.x[1]=age<duration/2 ? 375000:390000;
                GardenDraw d[2];CHECK(garden_ordnance_laser_build(&g,d,2,alpha)==2,"testactualviewbeams");
                memcpy(expected,base,sizeof(base));for(unsigned i=0;i<2;i++)reference_beam(&d[i],alpha);
                sel=frame++&1U;memcpy(other,fb(sel^1U),sizeof(other));
                CHECK(garden_render_frame(d,2)==GPU_OK,"production renderer beams inactualCPU/GPU paths");
                CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"fullRGB565 matches independent source sampling, clipping and2FB erases");
                CHECK(!memcmp(other,fb(sel^1U),sizeof(other)),"front/otherphysicalFB neverwritten");
            }
        }
        for(unsigned b=0;b<2;b++) { sel=b;CHECK(garden_render_frame(NULL,0)==GPU_OK,"off clears history");CHECK(!memcmp(base,fb(sel),sizeof(base)),"no residual laserpixels"); }
    }
    /* The source's bright core is a literal13pixelstrip, not its glow extent. */
    for(unsigned frameid=0;frameid<2;frameid++) {
        const garden_sprite_t *s=garden_asset(GARDEN_ASSET_ORDNANCE_BEAM_BASE+frameid);
        CHECK(s->width==25 && s->anchor_x==12 && s->anchor_y==0,"actualtexture pivot geometry");
        for(unsigned y=0;y<s->height;y++) for(unsigned x=0;x<25;x++) {
            uint16_t p=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+y*garden_sprite_stride(s)+x*2U);
            unsigned bright=((p>>11)&31U)>=27U && ((p>>5)&63U)>=45U && (p&31U)>=27U;
            CHECK(bright==(x>=6 && x<=18),"bright13pxkernel exactlyequalsclosedcollisionwidth;glowstaysdim");
        }
    }
}
static void events(void)
{
    GardenGame g;state(&g);g.tick=1;g.boss.active=0;g.combat.tick=1;g.combat.emitted_total=GARDEN_COMBAT_EVENT_MAX;
    g.combat.count=GARDEN_COMBAT_EVENT_MAX;g.enemies[0]=(GardenEnemy){.x=320000,.y=180000,.active=1,.type=GARDEN_ENEMY_DARTER,.hp=300};
    g.player.active=1;g.player.x=320000;g.player.y=410000;g.demo_mode=1;
    for(unsigned i=0;i<GARDEN_COMBAT_EVENT_MAX;i++) g.combat.items[i]=(GardenHitEvent){
        .x=320000,.y=180000,.target_x=320000,.target_y=180000,.damage=10,.score_delta=1,.target_slot=0,.target_type=GARDEN_ENEMY_DARTER};
    GardenCombat c;GardenScene scene;GardenFeedback feedback;GardenFx fx;
    garden_combat_reset(&c);garden_scene_reset(&scene);garden_feedback_reset(&feedback);garden_fx_reset(&fx);
    garden_combat_observe(&c,&g);GardenDraw d[GARDEN_DRAW_MAX];
    unsigned n=garden_combat_build(&c,&scene,&feedback,&fx,&g,d,GARDEN_DRAW_MAX,1,0),labels=0,lastart=0,firstlabel=n;
    for(unsigned i=0;i<n;i++) {
        if(d[i].asset&GARDEN_DRAW_SCORE_BASE) { labels++;if(firstlabel==n)firstlabel=i; }
        if(d[i].asset>=GARDEN_ASSET_ORDNANCE_HIT_BASE && d[i].asset<GARDEN_ASSET_ORDNANCE_HIT_BASE+GARDEN_ASSET_ORDNANCE_HIT_COUNT)lastart=i;
        CHECK(d[i].asset!=GARDEN_ASSET_FX_RING32 && d[i].asset!=GARDEN_ASSET_FX_RING48,"no former yellowring hitfeedback");
    }
    CHECK(labels==33 && c.stats.events_observed==33 && !c.stats.event_rejected && !c.stats.popup_pool_exhausted,"33independent valid hits produce33independentlabels");
    CHECK(lastart<firstlabel,"textured impact artneveroverpaintsdamage labels");
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"mapactualaddresses inhostRAMonly");
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL,"actualatlasopen");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES && fgetc(f)==EOF,"exactatlasbytes");fclose(f);
    geometry();entities();pixels();events();
    printf("PASS ordnance_view checks=%u descriptor_bytes=%u popup_bytes=%u combat_bytes=%u mutable_view_bytes=0\n",checks,(unsigned)sizeof(garden_sprite_t),(unsigned)sizeof(GardenCombatPopup),(unsigned)sizeof(GardenCombat));
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
