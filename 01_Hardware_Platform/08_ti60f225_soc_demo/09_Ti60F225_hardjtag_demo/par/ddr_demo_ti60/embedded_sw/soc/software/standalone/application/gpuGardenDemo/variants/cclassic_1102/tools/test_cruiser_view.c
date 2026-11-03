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
#include "garden_motion.h"
#include "garden_stage.h"
#include "garden_actor_ui.h"
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
    g->laser.x[0]=g->boss.x+GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN;
    g->laser.x[1]=g->boss.x+GARDEN_BOSS_MUZZLE_RIGHT_X_PX*GARDEN_GRAIN;
    g->laser.y=g->boss.y+GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN;
}
static void geometry(void)
{
    GardenGame g;state(&g);GardenDraw out[3];
    GardenScene scene;garden_scene_reset(&scene);garden_scene_observe(&scene,&g);
    CHECK(garden_ordnance_laser_count(&g)==0,"OFF no phantomlaser");
    unsigned prior=0;
    unsigned last_offset=100U,last_muzzle_alpha=0U;
    for(unsigned t=0;t<40;t++) {
        g.laser.phase=GARDEN_LASER_WARN;g.laser.ticks=40-t;
        GardenGame copy=g;CHECK(garden_ordnance_laser_build(&g,out,2,1)==2,"both warning rails");
        CHECK(out[0].alpha>=prior && out[0].alpha>=4 && out[0].alpha<=28,"40tick warn monotonically accumulates");prior=out[0].alpha;
        CHECK(!(out[0].asset&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE)),"warning encodednonactive");
        CHECK(out[0].x==278 && out[1].x==363 && out[0].y==119,"actual collisionx/y worldpivot");
        CHECK(!memcmp(&copy,&g,sizeof(g)),"viewcore completelyreadonly");
        GardenSceneBossPose pose;CHECK(garden_scene_boss_pose(&scene,&g,&pose),"actual Boss pose");
        for(unsigned i=0;i<2;i++) CHECK(pose.wings[i].x==out[i].x && pose.wings[i].y==out[i].y,"displayed wingmuzzle matches authoritative beamorigin");
        GardenDraw muzzle[2];CHECK(garden_ordnance_muzzle_build(&g,muzzle,2,1)==2,"twoartchargeparticles");
        unsigned offset=(unsigned)abs(muzzle[0].x-out[0].x);
        CHECK(offset<=last_offset && muzzle[0].alpha>=last_muzzle_alpha,"warningparticles convergeandbrighten monotonically");
        CHECK(muzzle[0].asset>=GARDEN_ASSET_ORDNANCE_HIT_BASE && muzzle[0].asset<GARDEN_ASSET_ORDNANCE_HIT_BASE+GARDEN_ASSET_ORDNANCE_HIT_COUNT,"charge usesreal slashartsequence");
        last_offset=offset;last_muzzle_alpha=muzzle[0].alpha;
    }
    g.laser.phase=GARDEN_LASER_FIRE;g.laser.ticks=40;
    garden_ordnance_laser_build(&g,out,2,1);
    CHECK((out[0].asset&GARDEN_DRAW_LASER_WIDTH_MASK)==13 && (out[0].asset&GARDEN_DRAW_LASER_ACTIVE) && out[0].alpha==32,"active visiblecore exactly13pixels");
    GardenDraw muzzle[2];garden_ordnance_muzzle_build(&g,muzzle,2,1);
    CHECK(muzzle[0].asset==GARDEN_ASSET_ORDNANCE_BLAST_BASE && muzzle[0].x==out[0].x && muzzle[0].y==out[0].y,"FIRE beginswithactualblastflare onmuzzle");
    g.laser.phase=GARDEN_LASER_FADE;prior=32;
    for(unsigned t=0;t<12;t++) {
        g.laser.ticks=12-t;garden_ordnance_laser_build(&g,out,2,1);
        CHECK(out[0].alpha<=prior && (out[0].asset&GARDEN_DRAW_LASER_FADE),"12tick fade monotone");prior=out[0].alpha;
        garden_ordnance_muzzle_build(&g,muzzle,2,1);
        CHECK(muzzle[0].y>=out[0].y && muzzle[0].alpha<=24U,"fadeartflowsdownanddiminishes");
    }
    out[0].asset=0xbeef;CHECK(!garden_ordnance_laser_build(&g,out,1,1) && out[0].asset==0xbeef,"paired laser admission never truncates one side");
    GardenFeedback feedback;GardenFx fx;garden_feedback_reset(&feedback);garden_fx_reset(&fx);
    garden_feedback_observe(&feedback,&g);garden_fx_observe(&fx,&g);
    for(unsigned cap=0;cap<=GARDEN_DRAW_MAX;cap++) {
        GardenDraw d[GARDEN_DRAW_MAX+1];memset(d,0,sizeof(d));d[cap].asset=0xbeef;
        unsigned n=garden_scene_build(&scene,&feedback,&fx,&g,d,cap,1,0),beams=0;
        CHECK(n<=cap && d[cap].asset==0xbeef,"sceneextra2muzzles neverspillsdrawcapacity");
        for(unsigned i=0;i<n;i++) beams+=(d[i].asset&0xF000U)==GARDEN_DRAW_LASER_BASE;
        if(cap>=3) CHECK(beams==2,"actual paired dangerousrails admittedbeforeallcosmetics");
    }
    for(unsigned phase=0;phase<16;phase++) {
        unsigned laser=GARDEN_DRAW_LASER_BASE|(phase<<8)|GARDEN_DRAW_LASER_ACTIVE|13U;
        CHECK(garden_draw_form_source(laser)==laser,"scrollbits cannotmisclassifylaser asformingprojectile");
    }
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
    int cx=320+d->x,top=120+d->y,left=active ? cx-22:cx,right=active ? cx+23:cx+1;
    for(int y=top<120 ? 120:top;y<600;y++) for(int x=left<320 ? 320:left;x<right && x<960;x++) {
        unsigned phase=((d->asset&GARDEN_DRAW_LASER_SCROLL_MASK)>>GARDEN_DRAW_LASER_SCROLL_SHIFT)*3U%24U;
        unsigned sy=((unsigned)(y-top)+phase)%s->height,sx=(unsigned)(x-cx+22);
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
                g.tick++;g.laser.phase=(uint8_t)phase;g.laser.ticks=(uint16_t)(duration-age);
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
        CHECK(s->width==45 && s->anchor_x==22 && s->anchor_y==0,"actualtexture pivot geometry");
        for(unsigned y=0;y<s->height;y++) for(unsigned x=0;x<45;x++) {
            uint16_t p=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+y*garden_sprite_stride(s)+x*2U);
            unsigned bright=((p>>11)&31U)>=27U && ((p>>5)&63U)>=45U && (p&31U)>=27U;
            CHECK(bright==(x>=16 && x<=28),"bright13pxkernel exactlyequalsclosedcollisionwidth;glowstaysdim");
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
static void reference_form(const GardenDraw *d,unsigned alpha_on)
{
    unsigned id=garden_draw_form_source(d->asset);
    const garden_sprite_t *s=garden_asset(id);
    unsigned radius=d->asset&GARDEN_DRAW_FORM_GROW ? 4U:1U;
    unsigned alpha=alpha_on ? d->alpha:32U;
    for(unsigned sy=0;sy<s->height;sy++) for(unsigned sx=0;sx<s->width;sx++) {
        int dx=(int)sx-s->anchor_x,dy=(int)sy-s->anchor_y;
        if(dx<-(int)radius || dx>(int)radius || dy<-(int)radius || dy>(int)radius) continue;
        int x=320+d->x+dx,y=120+d->y+dy;
        if(x<320 || x>=960 || y<120 || y>=600) continue;
        uint16_t p=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+sy*garden_sprite_stride(s)+sx*2U);
        if(p!=GARDEN_KEY_COLOR) expected[y][x]=alpha==32U ? p:gpu_alpha_rgb565(p,expected[y][x],alpha);
    }
}
static void formation(void)
{
    initial();unsigned frame=0,visited=0;
    GardenGame g={0};GardenFx fx;
    for(unsigned type=1;type<=2;type++) for(unsigned colour=1;colour<=5;colour++) for(unsigned heading=0;heading<64;heading++) {
        unsigned source=garden_bullet_precision_id(type,colour,heading*16U);
        CHECK(garden_asset_is_enemy_bullet(source),"all640reanchoredsource IDs stillhazards");
        for(unsigned age=0;age<6;age++) {
            GardenEnemyBullet *b=&g.enemy_bullets[0];
            *b=(GardenEnemyBullet){.active=1,.age=(uint16_t)age,.type=(uint8_t)type,.colour=(uint8_t)colour,
                .x=320000,.y=250000,.cord_angle=(uint16_t)(heading*16U)};
            garden_fx_reset(&fx);garden_fx_observe(&fx,&g);
            GardenDraw drawn[4];unsigned n=garden_fx_build(&fx,&g,drawn,4,1);
            CHECK(n==1 && drawn[0].asset==garden_fx_enemy_asset(b,source),"every source uses samecorearming rule andreal sprite");
            CHECK(drawn[0].alpha==6+age*3 && garden_draw_form_source(drawn[0].asset)==source,"small/dim formingvisuallydistinctfromarmed32");
            /* Both growth stages and Alpha modes use the actual renderer. */
            if(age!=0 && age!=3) continue;
            for(unsigned alpha_on=0;alpha_on<2;alpha_on++) {
                if(garden_render_alpha_enabled()!=(int)alpha_on) garden_render_alpha_toggle();
                memcpy(expected,base,sizeof(base));reference_form(&drawn[0],alpha_on);
                sel=frame++&1U;memcpy(other,fb(sel^1U),sizeof(other));
                CHECK(garden_render_frame(drawn,n)==GPU_OK,"actual formingcroprenderer");
                CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"all640source formationpixels match independentcroppingoracle");
                CHECK(!memcmp(other,fb(sel^1U),sizeof(other)),"forming neverwritesfrontbuffer");
            }
        }
        g.enemy_bullets[0].age=6;
        garden_fx_reset(&fx);GardenDraw d[4];unsigned n=garden_fx_build(&fx,&g,d,4,1);
        CHECK(n==1 && d[0].asset==source && d[0].alpha==32,"firstarmedframeusescompletefullbrightart");visited++;
    }
    CHECK(visited==640 && frame==2560,"bounded all640family/headings and2560RGB565frames");
}
static unsigned sync_draw(GardenMotion *motion,GardenGame *g,GardenDraw *out)
{
    const GardenEnemyBullet *b=&g->enemy_bullets[0];
    unsigned source=garden_bullet_precision_id(b->type,b->colour,heading64_from_velocity(b->vx,b->vy,b->cord_angle));
    out[0]=(GardenDraw){g->player.x/GARDEN_GRAIN,g->player.y/GARDEN_GRAIN,GARDEN_DRAW_BANK_BASE|8U,32};
    out[1]=(GardenDraw){b->x/GARDEN_GRAIN,b->y/GARDEN_GRAIN,(uint16_t)garden_fx_enemy_asset(b,source),(uint8_t)garden_fx_enemy_alpha(b)};
    out[2]=(GardenDraw){g->player.x/GARDEN_GRAIN,g->player.y/GARDEN_GRAIN,GARDEN_ASSET_FOCUS_VULNERABLE,32};
    GardenGame copy=*g;unsigned n=garden_motion_apply_bullets(motion,g,out,3);
    CHECK(!memcmp(&copy,g,sizeof(copy)),"same-time motion leavesallgameplayreadonly");return n;
}
static void synchrony(void)
{
    GardenGame g={0};GardenMotion m;garden_motion_reset(&m);
    g.tick=100;g.mode=GARDEN_MODE_WAVES;g.lives=3;g.player.active=1;g.player.x=100000;g.player.y=350000;
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=1,.colour=1,.age=10,.x=110000,.y=350000,.vx=5900,.vy=0};
    garden_motion_observe(&m,&g);g.tick++;g.player.x+=5900;g.enemy_bullets[0].x+=5900;g.enemy_bullets[0].age++;
    garden_motion_observe(&m,&g);GardenDraw out[3];
    for(unsigned q8=0;q8<256;q8++) {
        garden_motion_set_fraction(&m,q8);CHECK(sync_draw(&m,&g,out)==1,"real EB match preserved");
        CHECK(out[0].x==out[2].x && out[0].y==out[2].y,"bankedplayer andactualmarker shareoneq8position");
        CHECK(out[1].x-out[2].x==10 && out[1].y==out[2].y,"parallel5.9px/tick true10pxgap remains10pxvisually, neverfalse4pxoverlap");
    }
    /* Forming entries also match and interpolate, never silently reject due
     * to their deliberately lower alpha or virtual crop asset encoding. */
    garden_motion_reset(&m);g.tick=200;g.enemy_bullets[0].age=2;garden_motion_observe(&m,&g);
    g.tick++;g.player.x+=5900;g.enemy_bullets[0].x+=5900;g.enemy_bullets[0].age=3;garden_motion_observe(&m,&g);
    garden_motion_set_fraction(&m,0);CHECK(sync_draw(&m,&g,out)==1 && out[1].x-out[2].x==10,"formingEBandpilot sameclock acrossgrowthstage");
    g.tick++;g.lives--;g.player.respawning=50;g.player.active=0;g.player.x=320000;g.player.y=410000;
    garden_motion_observe(&m,&g);CHECK(!m.pilot.tag && !m.pilot.smooth,"death/resetsclearplayerhistory");
    g.tick++;g.player.respawning=0;g.player.active=1;garden_motion_observe(&m,&g);garden_motion_set_fraction(&m,0);
    sync_draw(&m,&g,out);CHECK(out[0].x==320 && out[2].x==320 && !m.pilot.smooth,"firstrespawn snapsbodyandmarker, noghost interpolation");
    g.tick++;g.player.x=600000;garden_motion_observe(&m,&g);sync_draw(&m,&g,out);
    CHECK(out[0].x==600 && out[2].x==600 && !m.pilot.smooth,"teleportover64px snapsboth");
    unsigned saved_fraction=m.fraction_q8;g.paused=1;garden_motion_observe(&m,&g);garden_motion_set_fraction(&m,255);
    CHECK(m.fraction_q8==saved_fraction,"pausefreezes sharedvisualsample");
    CHECK(sizeof(GardenMotion)==2660,"pilotuses12Bwithinexistingpadding, nonewlargearray");
}

static void reference_sprite(const GardenDraw *d,unsigned alpha_on)
{
    const garden_sprite_t *s=garden_asset(d->asset);
    CHECK(s!=NULL,"reference real asset exists");
    unsigned alpha=alpha_on ? d->alpha:32U;
    for(unsigned sy=0;sy<s->height;sy++) for(unsigned sx=0;sx<s->width;sx++) {
        int x=GARDEN_X0+d->x+(int)sx-s->anchor_x,y=GARDEN_Y0+d->y+(int)sy-s->anchor_y;
        if(x<GARDEN_X0 || x>=GARDEN_X0+GARDEN_WIDTH || y<GARDEN_Y0 || y>=GARDEN_Y0+GARDEN_HEIGHT)continue;
        uint16_t value=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+sy*garden_sprite_stride(s)+sx*2U);
        if(value!=GARDEN_KEY_COLOR && alpha) expected[y][x]=alpha==32 ? value:gpu_alpha_rgb565(value,expected[y][x],alpha);
    }
}
static void cruiser_origins(void)
{
    const garden_sprite_t *body=garden_asset(GARDEN_ASSET_BOSS1),*left=garden_asset(GARDEN_ASSET_BOSS_WING_L),*right=garden_asset(GARDEN_ASSET_BOSS_WING_R);
    CHECK(body->width==128 && body->height==132 && body->anchor_x==64 && body->anchor_y==66,"new cruiser source128x132 exactbodypivot");
    CHECK(left->width==44 && left->height==72 && left->anchor_x==22 && left->anchor_y==69,"leftcroppedgun pivotonlastopaque barrelpixel");
    CHECK(right->width==44 && right->height==72 && right->anchor_x==23 && right->anchor_y==69,"rightcroppedgun asymmetricpivot onlastopaque barrelpixel");
    const garden_sprite_t *parts[2]={left,right};
    for(unsigned i=0;i<2;i++) {
        const garden_sprite_t *a=parts[i];
        uint16_t muzzle=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+a->offset+a->anchor_y*garden_sprite_stride(a)+a->anchor_x*2U);
        CHECK(muzzle!=GARDEN_KEY_COLOR,"real modelmuzzle isopaque not guessed boundingbox centre");
    }
    GardenGame g;garden_init(&g,12345);g.demo_mode=0;g.mode=GARDEN_MODE_BOSS;
    g.boss=(GardenBoss){.x=320123,.y=120456,.target_x=320123,.active=1,.hp=50000,.max_hp=50000,
        .attack=2,.move_ticks=1,.rhythm_state=GARDEN_BOSS_TRANSIT};g.player.grace=1000;
    garden_step(&g,0);
    CHECK(g.laser.phase==GARDEN_LASER_WARN && g.weapon_stats.laser_warnings==1,"actualcore schedule startsreal lasers");
    int32_t bx=g.boss.x,by=g.boss.y;
    CHECK(g.laser.x[0]==bx+GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN &&
          g.laser.x[1]==bx+GARDEN_BOSS_MUZZLE_RIGHT_X_PX*GARDEN_GRAIN &&
          g.laser.y==by+GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN,"actualcore railsuse exact asymmetric newbarrel offsets");
    GardenScene scene;garden_scene_reset(&scene);garden_scene_observe(&scene,&g);
    for(unsigned t=0;t<GARDEN_LASER_WARN_TICKS+GARDEN_LASER_FIRE_TICKS+GARDEN_LASER_FADE_TICKS-1;t++) {
        garden_step(&g,0);garden_scene_observe(&scene,&g);
        CHECK(g.boss.x==bx && g.boss.y==by,"asymmetric mountinverse haszero0.5px accumulationthroughWARN FIRE FADE");
        for(unsigned q=0;q<256;q+=17) {
            garden_scene_set_render_fraction(&scene,q);GardenSceneBossPose pose;GardenDraw beam[2];
            CHECK(garden_scene_boss_pose(&scene,&g,&pose) && garden_ordnance_laser_build(&g,beam,2,1)==2,"lockedrealfiringpose stillpresent");
            for(unsigned i=0;i<2;i++) CHECK(pose.wings[i].x==beam[i].x && pose.wings[i].y==beam[i].y,"newbarrelpixel andpreview/fire/fade exactsameworldpoint atallsubframes");
        }
    }
    for(unsigned side=0;side<2;side++) for(int edge=-1;edge<=1;edge++) {
        state(&g);g.mode=GARDEN_MODE_BOSS;g.demo_mode=0;g.lives=3;g.player.active=1;g.player.y=400000;
        g.laser.phase=GARDEN_LASER_FIRE;g.laser.ticks=20;
        g.player.x=g.laser.x[side]+GARDEN_LASER_HALF_WIDTH+9000+edge;
        g.boss.move_ticks=100;garden_step(&g,0);
        CHECK(g.weapon_stats.laser_hits==(edge<=0 ? 1U:0U) && g.lives==(edge<=0 ? 2U:3U),"actualcollisioncore boundary shares renderedmount; no art-only beam shift");
    }
}
static void cruiser_health(void)
{
    for(unsigned kind=0;kind<3;kind++) for(unsigned kill=0;kill<2;kill++) {
        GardenGame g;state(&g);g.tick=2;g.boss.active=kind==2;g.combat.tick=2;g.combat.count=1;g.combat.emitted_total=1;
        unsigned slot=kind==2 ? GARDEN_MAX_ENEMIES:0,asset=kind==2 ? GARDEN_ASSET_BOSS1:kind ? GARDEN_ASSET_STOPPER1:GARDEN_ASSET_DARTER1;
        if(kind<2)g.enemies[0]=(GardenEnemy){.active=1,.x=320000,.y=180000,.type=(uint8_t)(kind+1),.hp=kind ? 3800:300};
        else g.boss.y=180000;
        int target_y=180;const garden_sprite_t *s=garden_asset(asset);GardenDraw bar;
        CHECK(garden_actor_ui_bar_geometry(&g,slot,&bar),"newmodel liveHPbar");
        CHECK(bar.y+8+4==target_y-s->anchor_y,"HPstrip fourpixelclearance aboveactual newmodel");
        g.combat.items[0]=(GardenHitEvent){.x=320000,.y=180000,.target_x=320000,.target_y=180000,.damage=95,
            .score_delta=1,.target_slot=(uint8_t)slot,.target_type=(uint8_t)(kind==2 ? 0:kind+1),.killed=(uint8_t)kill,
            .boss=(uint8_t)(kind==2),.kill_reward=kill ? (kind==2 ? 1000U:kind ? 80U:20U):0U};
        if(kill) {if(kind==2){g.boss.active=0;g.boss.hp=0;}else {g.enemies[0].active=0;g.enemies[0].hp=0;}}
        GardenCombat c;GardenScene scene;GardenFeedback feedback;GardenFx fx;
        garden_combat_reset(&c);garden_scene_reset(&scene);garden_feedback_reset(&feedback);garden_fx_reset(&fx);
        garden_combat_observe(&c,&g);GardenDraw d[GARDEN_DRAW_MAX];GardenGame copy=g;
        garden_combat_build(&c,&scene,&feedback,&fx,&g,d,GARDEN_DRAW_MAX,1,0);
        CHECK(!memcmp(&copy,&g,sizeof(g)),"newmodel observer writesnogameplay");
        CHECK(c.stats.popups_spawned==1 && c.popups[0].anchor_policy==1,"liveandkillinghit labels stayheadtop");
        for(unsigned age=0;age<GARDEN_COMBAT_POPUP_TICKS;age++) {
            c.popups[0].age=(uint8_t)age;
            for(unsigned q=0;q<256;q+=51) {
                garden_combat_set_render_fraction(&c,q);GardenDraw label;
                CHECK(garden_combat_popup_draw(&c,0,1,&label),"independentreal damage label remainsvisible");
                CHECK(label.y+16<=bar.y && label.y>=2,"entirefloatingdamage label staysabove ownHPstrip notovernewship");
            }
        }
    }
}
static void cruiser_pixels(void)
{
    initial();GardenGame g;state(&g);GardenScene scene;GardenFeedback feedback;GardenFx fx;
    garden_scene_reset(&scene);garden_feedback_reset(&feedback);garden_fx_reset(&fx);
    garden_scene_observe(&scene,&g);unsigned frame=0;
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned alpha=0;alpha<2;alpha++) {
        garden_render_mode((int)cpu);if(garden_render_alpha_enabled()!=(int)alpha)garden_render_alpha_toggle();
        for(unsigned rhythm=GARDEN_BOSS_TRANSIT;rhythm<=GARDEN_BOSS_WAIT;rhythm++) for(unsigned sample=0;sample<5;sample++) {
            g.boss.rhythm_state=(uint8_t)rhythm;g.boss.charge_ticks=20;g.boss.rest_ticks=80;
            g.boss.rhythm_ticks=(uint16_t)(rhythm==GARDEN_BOSS_REST ? 80-sample*16:20-sample*4);
            g.boss.flash=(uint16_t)sample;g.tick++;garden_scene_observe(&scene,&g);garden_scene_set_render_fraction(&scene,sample*51);
            g.laser.phase=(uint8_t)(sample%4);g.laser.ticks=sample%4==3 ? 12:40;
            GardenGame copy=g;GardenDraw d[GARDEN_DRAW_MAX];unsigned n=garden_scene_build(&scene,&feedback,&fx,&g,d,GARDEN_DRAW_MAX,alpha,0);
            CHECK(!memcmp(&copy,&g,sizeof(g)),"allposeandflash buildsleave gameplayreadonly");
            unsigned hull=0,pods=0;memcpy(expected,base,sizeof(base));
            for(unsigned i=0;i<n;i++) {
                hull+=d[i].asset==GARDEN_ASSET_BOSS1 || d[i].asset==GARDEN_ASSET_FLASH_BOSS1;
                pods+=d[i].asset==GARDEN_ASSET_BOSS_WING_L || d[i].asset==GARDEN_ASSET_BOSS_WING_R;
                if((d[i].asset&0xF000U)==GARDEN_DRAW_LASER_BASE)reference_beam(&d[i],alpha);
                else reference_sprite(&d[i],alpha);
            }
            CHECK(hull==1 && pods==2,"allrhythm/flash posesretainnew cruiserbodyandbothnewpods neveroldshield substitutes");
            sel=frame++&1U;memcpy(other,fb(sel^1U),sizeof(other));CHECK(garden_render_frame(d,n)==GPU_OK,"newboss composed withreal production renderer");
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"newbody/pods/charge/laser fullframepixels equalindependentatlasoracle");
            CHECK(!memcmp(other,fb(sel^1U),sizeof(other)),"newmodelnever touchesother/frontframebuffer");
        }
        for(unsigned colour=1;colour<=5;colour++) {
            unsigned id=garden_bullet_precision_id(2,colour,0);const garden_sprite_t *s=garden_asset(id);
            CHECK(s->width==16 && s->height==16,"new16x16orb geometry");
            for(unsigned angle=0;angle<64;angle++) { const garden_sprite_t *alias=garden_asset(garden_bullet_precision_id(2,colour,angle*16));
                CHECK(alias->width==s->width && alias->height==s->height && alias->anchor_x==s->anchor_x && alias->anchor_y==s->anchor_y &&
                      !memcmp((void *)(uintptr_t)(ASSET_BASE_ADDR+alias->offset),(void *)(uintptr_t)(ASSET_BASE_ADDR+s->offset),s->width*s->height*2U),
                      "all64orbheadings haveidenticalcircular pixelsandcentre; pairedsourceoffsets preservehardwareprobe coverage"); }
            GardenDraw d={320,250,(uint16_t)id,(uint8_t)(alpha ? 16:32)};
            memcpy(expected,base,sizeof(base));reference_sprite(&d,alpha);sel=frame++&1U;
            CHECK(garden_render_frame(&d,1)==GPU_OK && !memcmp(expected,fb(sel),sizeof(expected)),"armed neworb keepsall16x16pixels without olddiamondcrop");
        }
        for(unsigned b=0;b<2;b++){sel=b;CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(base,fb(sel),sizeof(base)),"bothphysicalFB eraseallnew hull/pod/orb pixels withoutresidue");}
    }
    GardenStage stage;garden_stage_reset(&stage);g.mode=GARDEN_MODE_VICTORY;g.boss.active=0;
    stage.initialized=1;stage.last_mode=GARDEN_MODE_VICTORY;stage.victory_tick=100;stage.death_x=320;stage.death_y=120;
    for(unsigned age=0;age<GARDEN_STAGE_DEATH_TICKS;age++) {
        stage.last_tick=100+age;GardenDraw d[GARDEN_DRAW_MAX];unsigned n=garden_stage_append_effects(&stage,&g,d,0,GARDEN_DRAW_MAX,1);
        CHECK(n<=GARDEN_STAGE_DEATH_DRAW_MAX,"newhull death keepsoldbounded stagedexplosionbudget");
        if(age<32){CHECK(d[0].asset==GARDEN_ASSET_BOSS1 && d[1].asset==GARDEN_ASSET_BOSS_WING_L && d[2].asset==GARDEN_ASSET_BOSS_WING_R,"deathusesthesamenewhull/pods");
            CHECK(d[1].x==320+GARDEN_BOSS_MUZZLE_LEFT_X_PX-(int)age*2 && d[2].x==320+GARDEN_BOSS_MUZZLE_RIGHT_X_PX+(int)age*2 &&
                  d[1].y==120+GARDEN_BOSS_MUZZLE_Y_PX+(int)age/2,"deathpodorigincomesfromactualnewmuzzles nooldwingpositionjump");}
    }
}
static void cruiser_contracts(void)
{ cruiser_origins();cruiser_health();cruiser_pixels(); }

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"mapactualaddresses inhostRAMonly");
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL,"actualatlasopen");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES && fgetc(f)==EOF,"exactatlasbytes");fclose(f);
    geometry();entities();pixels();events();formation();synchrony();cruiser_contracts();
    printf("PASS cruiser_view checks=%u descriptor_bytes=%u popup_bytes=%u combat_bytes=%u motion_bytes=%u mutable_view_bytes=0\n",checks,(unsigned)sizeof(garden_sprite_t),(unsigned)sizeof(GardenCombatPopup),(unsigned)sizeof(GardenCombat),(unsigned)sizeof(GardenMotion));
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
