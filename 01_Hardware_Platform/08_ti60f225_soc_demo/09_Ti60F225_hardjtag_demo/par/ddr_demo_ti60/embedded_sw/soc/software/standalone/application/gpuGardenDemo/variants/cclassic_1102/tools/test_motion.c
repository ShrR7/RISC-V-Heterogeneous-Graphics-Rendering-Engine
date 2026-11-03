/* SPDX-License-Identifier: GPL-3.0-or-later
 * True production bullet-motion/pipeline host contracts; no hardware access.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "garden_motion.h"
#include "garden_combat.h"
#include "garden_actor_ui.h"
#include "garden_assets.h"
static unsigned checks,frames,real_steps,changed,popup_frames,marker_frames;
#define CHECK(c,m) do {checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m);exit(2);}}while(0)
static GardenGame g,saved,baseline;
static GardenMotion motion;
static GardenFx fx;
static GardenScene scene;
static GardenFeedback feedback;
static GardenCombat combat;
static GardenActorUi ui;
extern unsigned garden_motion_host_q4(int32_t raw,int16_t *result);
static unsigned old_q4(int32_t raw,int16_t *result)
{
    int32_t whole=raw/GARDEN_GRAIN;
    if(whole<-2048 || whole>2047) return 0U;
    int32_t value=whole*16+(raw%GARDEN_GRAIN)*16/GARDEN_GRAIN;
    if(value<INT16_MIN || value>INT16_MAX) return 0U;
    *result=(int16_t)value;return 1U;
}
static void exact_fast_q4(void)
{
    unsigned points=0U;
    for(int32_t raw=-2048064;raw<=2048064;raw++) {
        int16_t before=-12345,after=-12345;
        unsigned expected=old_q4(raw,&before),actual=garden_motion_host_q4(raw,&after);
        CHECK(actual==expected,"fast Q4 accepts exactly the old signed valid range at every raw unit");
        CHECK(before==after,"fast Q4 is bit-exact to the original quotient/remainder converter, including rejected-output sentinel");
        points++;
    }
    const int32_t extremes[]={INT32_MIN,INT32_MIN+1,INT32_MAX-1,INT32_MAX,-3000000,3000000};
    for(unsigned i=0;i<sizeof(extremes)/sizeof(*extremes);i++) {
        int16_t a=2222,b=2222;unsigned old=old_q4(extremes[i],&a),fast=garden_motion_host_q4(extremes[i],&b);
        CHECK(old==fast && a==b,"unsigned magnitude/range guard safely rejects signed32 extremes");
    }
    printf("PASS fast_Q4 differential_points=%u raw_min=-2048064 raw_max=2048064 exact_old_semantics=1\n",points);
}
static void empty(void)
{
    memset(&g,0,sizeof(g));g.tick=100U;g.mode=GARDEN_MODE_BOSS;
    garden_motion_reset(&motion);garden_fx_reset(&fx);garden_fx_set_flags(&fx,0U);
}
static unsigned id(const GardenEnemyBullet *b)
{return garden_bullet_precision_id(b->type,b->colour,heading64_from_velocity(b->vx,b->vy,b->cord_angle));}
static int same(GardenDraw a,GardenDraw b)
{return a.x==b.x && a.y==b.y && a.asset==b.asset && a.alpha==b.alpha;}
static unsigned fx_list(GardenDraw *d,unsigned cap)
{return garden_fx_build(&fx,&g,d,cap,1U);}
static void advance(int vx,int vy)
{
    g.tick++;g.enemy_bullets[0].age++;g.enemy_bullets[0].x+=vx;g.enemy_bullets[0].y+=vy;
    garden_motion_observe(&motion,&g);
}
static void temporal(unsigned numerator,unsigned denominator,int start_x,int start_y,int vx,int vy)
{
    empty();g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=2,.colour=5,.x=start_x-vx,.y=start_y-vy,.age=9,.vx=vx,.vy=vy,.ttl=400};
    garden_motion_observe(&motion,&g);advance(vx,vy); /* valid prior/current segment at t=0 */
    uint64_t next=25000U;int last_x=0;
    for(unsigned frame=0;frame<48U;frame++) {
        uint64_t us=(uint64_t)frame*1000000U*denominator/numerator;
        while(next<=us) {advance(vx,vy);next+=25000U;}
        garden_motion_set_fraction(&motion,(unsigned)((us%25000U)*256U/25000U));
        GardenDraw d[1],original;CHECK(fx_list(d,1)==1U,"single real bullet fixture admitted");original=d[0];saved=g;
        CHECK(garden_motion_apply_bullets(&motion,&g,d,1)==1U && motion.stats.interpolated==1U,"independent render timestamp uses valid previous/current segment");
        double expected_x=(double)(start_x-vx)/1000.0+(double)vx*(double)us/25000000.0;
        double expected_y=(double)(start_y-vy)/1000.0+(double)vy*(double)us/25000000.0;
        CHECK(fabs(d[0].x-expected_x)<1.0 && fabs(d[0].y-expected_y)<1.0,"Q4/Q8 interpolation and one raster rounding remain within1pixel of delayed continuous path");
        CHECK(d[0].asset==original.asset && d[0].alpha==original.alpha && !memcmp(&saved,&g,sizeof(g)),"heading/colour/opacity and every core byte unchanged");
        if(numerator==60U && denominator==1U && vx==6000 && frame) CHECK(d[0].x-last_x>=3 && d[0].x-last_x<=5,"constant60Hz motion replaces0/6/6 with approximately4pixel steps");
        last_x=d[0].x;frames++;
    }
}
static void trajectories(void)
{
    temporal(60,1,100000,100000,6000,3500);
    temporal(576,10,100000,100000,6000,3500);
    temporal(342,10,100000,100000,6000,3500);
    temporal(327,10,-100000,-100000,3700,-2500);
    temporal(60,1,-100000,100000,-3700,2500);
    puts("PASS timestamp rates=60,57.6,34.2,32.7Hz constant_diagonal_negative_catchup=1 interpolation_error_lt1px=1");
}
static void lifecycle(void)
{
    empty();g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=1,.colour=3,.x=100000,.y=200000,.age=1,.vx=6000};
    GardenDraw d[4],a;garden_motion_observe(&motion,&g);garden_motion_set_fraction(&motion,0U);fx_list(d,1);a=d[0];
    garden_motion_apply_bullets(&motion,&g,d,1);CHECK(same(a,d[0]) && !motion.enemy[0].smooth,"newborn never interpolates from old slot/origin");
    advance(6000,0);garden_motion_set_fraction(&motion,128U);fx_list(d,1);garden_motion_apply_bullets(&motion,&g,d,1);
    CHECK(d[0].x==103 && motion.enemy[0].smooth,"next same-life step gives midpoint");
    GardenMotionSlot history=motion.enemy[0];garden_motion_observe(&motion,&g);
    CHECK(!memcmp(&history,&motion.enemy[0],sizeof(history)),"same-tick observation keeps previous/current samples intact");
    g.paused=1;garden_motion_observe(&motion,&g);garden_motion_set_fraction(&motion,250U);
    CHECK(motion.fraction_q8==128U,"pause freezes fraction setter");
    for(unsigned i=0;i<20;i++) {fx_list(d,1);garden_motion_apply_bullets(&motion,&g,d,1);CHECK(d[0].x==103,"paused presentation remains fixed across repeated builds");}
    g.paused=0;garden_motion_observe(&motion,&g);garden_motion_set_fraction(&motion,192U);CHECK(motion.fraction_q8==192U,"resume restores fraction control");
    g.tick++;g.enemy_bullets[0].age=1;g.enemy_bullets[0].x=350000;garden_motion_observe(&motion,&g);
    garden_motion_set_fraction(&motion,0);fx_list(d,1);a=d[0];garden_motion_apply_bullets(&motion,&g,d,1);
    CHECK(same(a,d[0]) && !motion.enemy[0].smooth && motion.stats.reused,"same slot age reset snaps to its new projectile");
    advance(6000,0);g.tick++;g.enemy_bullets[0].age++;g.enemy_bullets[0].colour=5;garden_motion_observe(&motion,&g);
    CHECK(!motion.enemy[0].smooth,"colour/type identity change cannot reuse a previous path");
    advance(6000,0);g.tick++;g.enemy_bullets[0].age++;g.enemy_bullets[0].x+=64001;garden_motion_observe(&motion,&g);
    CHECK(!motion.enemy[0].smooth && motion.stats.teleports==1U,"teleport beyond64pixels snaps even if age/type survive");
    g.tick++;g.enemy_bullets[0].active=0;garden_motion_observe(&motion,&g);CHECK(!motion.enemy[0].tag && fx_list(d,4)==0U,"dead bullet is removed immediately, no interpolation corpse");
    g.tick++;g.enemy_bullets[0].active=1;g.enemy_bullets[0].age=20;g.enemy_bullets[0].x=50000;garden_motion_observe(&motion,&g);
    CHECK(!motion.enemy[0].smooth,"birth after inactive slot snaps even for malformed nonnew age");
    advance(6000,0);g.tick=0;garden_motion_observe(&motion,&g);CHECK(!motion.enemy[0].smooth && motion.stats.epochs,"restart tick rollback clears interpolation");
    advance(6000,0);g.mode=GARDEN_MODE_WAVES;garden_motion_observe(&motion,&g);CHECK(!motion.enemy[0].smooth,"mode epoch resets even without a different tick");
    advance(6000,0);g.tick+=3;g.enemy_bullets[0].age+=3;garden_motion_observe(&motion,&g);CHECK(!motion.enemy[0].smooth,"missed observer samples fail closed rather than blend over an unknown trajectory");
    garden_motion_reset(&motion);CHECK(!motion.initialized && !motion.enemy[0].tag,"explicit reset clears every history");
    puts("PASS lifecycle newborn_reuse_pause_same_tick_multicatchup_restart_mode_teleport_death=1");
}
static void malformed_and_matching(void)
{
    empty();g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=2,.colour=2,.x=100000,.y=100000,.age=10,.vx=4000};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.age=10,.x=200000,.y=300000};
    garden_motion_observe(&motion,&g);g.tick++;g.enemy_bullets[0].age++;g.enemy_bullets[0].x+=4000;
    g.player_bullets[0].age++;g.player_bullets[0].y-=5000;garden_motion_observe(&motion,&g);garden_motion_set_fraction(&motion,128);
    GardenDraw d[8],original[8];unsigned n=fx_list(d+2,6);
    d[0]=(GardenDraw){-100,-200,GARDEN_ASSET_SHOT,16};d[1]=(GardenDraw){-200,-100,(uint16_t)id(&g.enemy_bullets[0]),32};
    d[n+2]=(GardenDraw){200,295,GARDEN_ASSET_SHOT,32}; /* identical later decoration after all actual PB slots */
    memcpy(original,d,sizeof(d));saved=g;
    CHECK(garden_motion_apply_bullets(&motion,&g,d,n+3)==2U,"only actual ordered PB/EB slots are accepted, not extra decorative entries");
    CHECK(same(d[0],original[0]) && same(d[1],original[1]) && same(d[n+2],original[n+2]),"wrong alpha/coordinate and later decorativeSHOT entries remain exact");
    CHECK(d[2].x==200 && d[2].y==298 && d[3].x==102 && d[3].y==100,"ordered production PB and EB paths independently interpolated");
    CHECK(!memcmp(&saved,&g,sizeof(g)),"matching never mutates game state");
    const int32_t invalids[]={INT32_MIN,INT32_MAX,-3000000,3000000};
    for(unsigned j=0;j<4;j++) {
        g.tick++;g.enemy_bullets[0].age++;g.enemy_bullets[0].x=invalids[j];garden_motion_observe(&motion,&g);
        CHECK(!motion.enemy[0].tag && !motion.enemy[0].smooth,"malformed position cannot wrap Q4 history");
        n=fx_list(d,8);memcpy(original,d,sizeof(d));garden_motion_apply_bullets(&motion,&g,d,n);
        for(unsigned i=0;i<n;i++) if(garden_asset_is_enemy_bullet(d[i].asset)) CHECK(same(d[i],original[i]),"out of range snaps to original renderer entry without overflow");
    }
    CHECK(motion.stats.range_rejected==4U,"each rejected malformed observation counted");
    garden_motion_set_fraction(&motion,UINT_MAX);CHECK(motion.fraction_q8==255U,"fraction saturates without wrap");
    CHECK(garden_motion_apply_bullets(NULL,&g,d,8)==0 && garden_motion_apply_bullets(&motion,NULL,d,8)==0 &&
          garden_motion_apply_bullets(&motion,&g,NULL,8)==0,"null apply inputs bounded");
    garden_motion_reset(NULL);garden_motion_observe(NULL,&g);garden_motion_observe(&motion,NULL);garden_motion_set_fraction(NULL,128);
    CHECK(garden_motion_stats(NULL)==NULL,"null statistics query bounded");
    puts("PASS malformed_INT32_Q4_range_guards decorSHOT_stable_order_assets_opacity_null=1");
}
static void exact_asset_matching(void)
{
    GardenDraw draw;unsigned tested=0U;
    for(unsigned type=1;type<=2;type++) for(unsigned colour=1;colour<=5;colour++) for(unsigned bin=0;bin<64;bin++) {
        empty();double a=(double)bin*2.0*acos(-1.0)/64.0;
        GardenEnemyBullet *b=&g.enemy_bullets[0];
        *b=(GardenEnemyBullet){.active=1,.age=10,.type=(uint8_t)type,.colour=(uint8_t)colour,
            .x=100000,.y=100000,.vx=(int32_t)llround(cos(a)*1000000.0),.vy=(int32_t)llround(sin(a)*1000000.0)};
        garden_motion_observe(&motion,&g);advance(6000,4000);garden_motion_set_fraction(&motion,128U);
        unsigned expected=garden_bullet_precision_id(type,colour,bin*16U);
        for(unsigned asset=0;asset<GARDEN_ASSET_COUNT;asset++) {
            draw=(GardenDraw){106,104,(uint16_t)asset,32};GardenDraw before=draw;
            unsigned matched=garden_motion_apply_bullets(&motion,&g,&draw,1);
            CHECK(matched==(asset==expected),"decoded type/colour/rotation validates exactly the original public helper ID, rejecting every wrong atlas slot");
            if(matched) CHECK(draw.x==103 && draw.y==102 && draw.asset==expected && draw.alpha==32,"only exact real candidate receives interpolation");
            else CHECK(same(draw,before),"all839 wrong IDs remain byte-semantically unchanged");
            tested++;
        }
    }
    /* Public helper historically normalizes malformed colour/type to3/Dart.
     * Preserve that matching result while inactive identity safely snaps. */
    const unsigned types[]={0U,3U},colours[]={0U,6U};
    for(unsigned t=0;t<2;t++) for(unsigned c=0;c<2;c++) {
        empty();GardenEnemyBullet *b=&g.enemy_bullets[0];*b=(GardenEnemyBullet){.active=1,.age=10,.type=(uint8_t)types[t],.colour=(uint8_t)colours[c],.x=100000,.y=100000,.vx=1000,.vy=200};
        garden_motion_observe(&motion,&g);unsigned expected=id(b);
        draw=(GardenDraw){100,100,(uint16_t)expected,32};CHECK(garden_motion_apply_bullets(&motion,&g,&draw,1)==1U && draw.x==100 && motion.stats.snapped==1U,"invalid core type/colour preserve helper fallback and fail-closed snap semantics");
    }
    printf("PASS exact_asset_candidates=%u all640families_and_directions=1 wrong_type_colour_rotation_rejected=1\n",tested);
}
static void capacities(void)
{
    empty();for(unsigned i=0;i<24;i++) g.player_bullets[i]=(GardenPlayerBullet){.active=1,.age=10,.x=(int32_t)i*10000,.y=300000};
    for(unsigned i=0;i<192;i++) g.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=(uint8_t)(1U+i%2U),.colour=(uint8_t)(1U+i%5U),.age=10,.x=(int32_t)i*3100,.y=100000,.vx=4000};
    garden_motion_observe(&motion,&g);g.tick++;
    for(unsigned i=0;i<24;i++){g.player_bullets[i].age++;g.player_bullets[i].y-=6000;}
    for(unsigned i=0;i<192;i++){g.enemy_bullets[i].age++;g.enemy_bullets[i].x+=4000;}
    garden_motion_observe(&motion,&g);garden_motion_set_fraction(&motion,128);
    struct {uint32_t head[4];GardenDraw d[240];uint32_t tail[4];} block,before;
    for(unsigned cap=0;cap<=250;cap++) {
        memset(&block,0xA5,sizeof(block));unsigned n=fx_list(block.d,cap);before=block;saved=g;
        CHECK(garden_motion_apply_bullets(&motion,&g,block.d,n)==n,"every admitted full-pool projectile maps exactly once at every capacity");
        CHECK(motion.stats.player_bullets+motion.stats.enemy_bullets==n,"projectile match statistics exact");
        for(unsigned i=0;i<n;i++) CHECK(block.d[i].asset==before.d[i].asset && block.d[i].alpha==before.d[i].alpha,"pool order/type/colour/heading/alpha untouched");
        CHECK(!memcmp(block.head,before.head,sizeof(block.head)) && !memcmp(block.tail,before.tail,sizeof(block.tail)),"leading/trailing apply canaries unchanged");
        CHECK(!memcmp(block.d+n,before.d+n,(240U-n)*sizeof(GardenDraw)),"no write after list length at any capacity");
        CHECK(!memcmp(&saved,&g,sizeof(g)),"full-pool application pure");
    }
    puts("PASS full_pool=216 capacities0..250 order_alpha_canaries_pure=1");
}
static void observe_all(void)
{
    garden_fx_observe(&fx,&g);garden_feedback_observe(&feedback,&g);garden_scene_observe(&scene,&g);
    garden_combat_observe(&combat,&g);garden_actor_ui_observe(&ui,&g);garden_motion_observe(&motion,&g);
}
static void real_pipeline(void)
{
    garden_init(&g,12345U);baseline=g;garden_fx_reset(&fx);garden_fx_set_flags(&fx,GARDEN_FX_FLASH|GARDEN_FX_RING);
    garden_feedback_reset(&feedback);garden_scene_reset(&scene);garden_combat_reset(&combat);garden_actor_ui_reset(&ui);garden_motion_reset(&motion);
    observe_all();GardenDraw d[240],old[240];
    for(unsigned step=0;step<12000U;step++) {
        unsigned input=0;if(step%3500U==1800 || step%3500U==1825) input=GARDEN_INPUT_PAUSE;
        if(step%3500U==3300) input=GARDEN_INPUT_RESTART;
        garden_step(&g,(uint16_t)input);garden_step(&baseline,(uint16_t)input);observe_all();
        unsigned fraction=(step*73U)&255U;
        garden_motion_set_fraction(&motion,fraction);garden_scene_set_render_fraction(&scene,fraction);garden_combat_set_render_fraction(&combat,fraction);
        unsigned reserve=garden_actor_ui_bar_count(&g),n=garden_combat_build(&combat,&scene,&feedback,&fx,&g,d,240U-reserve,1,0);
        garden_actor_ui_apply(&ui,&g,d,n);n=garden_actor_ui_append_bars(&ui,&g,d,n,240);memcpy(old,d,n*sizeof(*d));saved=g;
        unsigned matches=garden_motion_apply_bullets(&motion,&g,d,n);
        unsigned expected=0;for(unsigned i=0;i<24;i++) expected+=g.player_bullets[i].active!=0;
        for(unsigned i=0;i<192;i++) expected+=g.enemy_bullets[i].active!=0;
        CHECK(matches==expected && !fx.stats.essential_dropped,"complete true combat/scene/bank/HP pipeline matches every admitted real projectile");
        for(unsigned i=0;i<n;i++) {
            CHECK(d[i].asset==old[i].asset && d[i].alpha==old[i].alpha,"complete pipeline retains64direction/fivecolour IDs, opacity and list order");
            if(d[i].asset!=GARDEN_ASSET_SHOT && !garden_asset_is_enemy_bullet(d[i].asset)) {
                CHECK(same(d[i],old[i]),"model/wing/core/cue/HP/head damage/focus marker entries are untouched");
                if(d[i].asset&0x8000U) popup_frames++;
                if(d[i].asset==GARDEN_ASSET_FOCUS_VULNERABLE || d[i].asset==GARDEN_ASSET_FOCUS_GRACE) marker_frames++;
            } else changed+=d[i].x!=old[i].x || d[i].y!=old[i].y;
        }
        CHECK(!memcmp(&saved,&g,sizeof(g)) && !memcmp(&g,&baseline,sizeof(g)),"12000 actual core steps incl pause/restart remain completely byte-identical to no-motion baseline");
        real_steps++;
    }
    CHECK(changed>10000U && popup_frames>100U && marker_frames>1000U && motion.stats.match_rejected==0U,"actual movement, independently born damage and true marker exercised without ambiguous matching");
    printf("PASS production_pipeline real_steps=%u changed_bullet_draws=%u independent_popup_draws=%u marker_draws=%u match_rejected=%u\n",real_steps,changed,popup_frames,marker_frames,motion.stats.match_rejected);
}
int main(void)
{
    CHECK(sizeof(GardenMotionSlot)==12U && sizeof(GardenMotion)<=2752U,"fixed compact memory, no copied full game snapshot");
    exact_fast_q4();trajectories();lifecycle();malformed_and_matching();exact_asset_matching();capacities();real_pipeline();
    printf("SUMMARY checks=%u frames=%u real_steps=%u state_bytes=%u slot_bytes=%u changed=%u popups=%u markers=%u\n",checks,frames,real_steps,(unsigned)sizeof(GardenMotion),(unsigned)sizeof(GardenMotionSlot),changed,popup_frames,marker_frames);
    return 0;
}
