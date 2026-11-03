/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real core/feedback/FX/scene contracts; host only, no MMIO or timing claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_scene.h"
#include "garden_assets.h"

static unsigned checks;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)

static void start(GardenGame *g,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{
    garden_init(g,12345U);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->player_bullets,0,sizeof(g->player_bullets));
    memset(g->enemy_bullets,0,sizeof(g->enemy_bullets));
    memset(g->effects,0,sizeof(g->effects));
    g->tick=100; g->mode=GARDEN_MODE_BOSS; g->demo_mode=0;
    g->player=(GardenPlayer){.active=0};
    g->boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=50000,.max_hp=50000,
        .attack=1,.phase_ticks=300,.recycle=170};
    garden_scene_reset(s); garden_feedback_reset(f); garden_fx_reset(fx);
}

static void observe(GardenGame *g,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{
    garden_scene_observe(s,g); garden_feedback_observe(f,g); garden_fx_observe(fx,g);
}

static unsigned shots(const GardenGame *g)
{ return g->boss_attack_shots[0]+g->boss_attack_shots[1]+g->boss_attack_shots[2]; }

static unsigned find(const GardenDraw *d,unsigned n,unsigned asset)
{
    for (unsigned i=0;i<n;i++) if (d[i].asset==asset) return i;
    return n;
}

static unsigned count_asset(const GardenDraw *d,unsigned n,unsigned asset)
{
    unsigned c=0; for (unsigned i=0;i<n;i++) c+=d[i].asset==asset; return c;
}

static void anticipation_matches_real_core(void)
{
    static const unsigned moves[]={0,1,5,11,12,40,100};
    static const unsigned recycle[]={0,1,5,12,13,30,170};
    static const unsigned phase[]={0,1,5,12,20,300};
    GardenGame g,future; GardenScene s; GardenFeedback f; GardenFx fx;
    for (unsigned a=1;a<=3;a++) for (unsigned m=0;m<7;m++)
        for (unsigned r=0;r<7;r++) for (unsigned p=0;p<6;p++) {
            start(&g,&s,&f,&fx);
            g.boss.attack=(uint8_t)a; g.boss.move_ticks=(uint16_t)moves[m];
            g.boss.recycle=(uint16_t)recycle[r]; g.boss.phase_ticks=(uint16_t)phase[p];
            observe(&g,&s,&f,&fx); future=g;
            unsigned next=0;
            for (unsigned t=1;t<=GARDEN_SCENE_CHARGE_TICKS;t++) {
                unsigned before=shots(&future); garden_step(&future,0);
                if (shots(&future)!=before) { next=t; break; }
            }
            CHECK(s.charge_countdown==next,"countdown exactly matches next real fire within12ticks, including transit/phase");
            CHECK(!s.stats.attack_shots_observed && !s.burst_ticks,"initial snapshot cannot invent an observed shot");
        }
    start(&g,&s,&f,&fx); g.boss.y=-80000; g.boss.vy=3000;
    g.boss.move_ticks=100; g.boss.recycle=0; observe(&g,&s,&f,&fx);
    unsigned warnings=0;
    for (unsigned i=0;i<101;i++) {
        if (s.charge_countdown) warnings++;
        unsigned before=shots(&g); garden_step(&g,0); observe(&g,&s,&f,&fx);
        if (i<100) CHECK(shots(&g)==before,"cold entrance returns even at final movement tick");
        else CHECK(shots(&g)==1 && s.burst_ticks==4,"first attack has real counter and a four-tick burst");
    }
    CHECK(warnings==12 && s.stats.charge_windows==1,"first cold attack receives all twelve anticipation ticks");
    CHECK(s.stats.attack_shots_observed==1,"actual trigger observed once");
    puts("PASS: 882 actual-core forecasts, phase-cancelled recycle, transit final tick, cold first-shot anticipation");
}

static void observe_epochs_and_actual_cycle(void)
{
    GardenGame g; GardenScene s,saved; GardenFeedback f; GardenFx fx;
    start(&g,&s,&f,&fx); g.boss.recycle=1; observe(&g,&s,&f,&fx);
    garden_step(&g,0); observe(&g,&s,&f,&fx);
    CHECK(s.burst_ticks==4 && s.stats.attack_shots_observed==1,"actual shot creates burst");
    saved=s; for (unsigned i=0;i<5;i++) garden_scene_observe(&s,&g);
    CHECK(!memcmp(&s,&saved,sizeof(s)),"same-tick calls are idempotent");
    garden_step(&g,GARDEN_INPUT_PAUSE); garden_scene_observe(&s,&g);
    CHECK(!memcmp(&s,&saved,sizeof(s)),"paused tick freezes scene history and counters");
    garden_step(&g,GARDEN_INPUT_PAUSE); observe(&g,&s,&f,&fx);
    CHECK(s.burst_ticks==3,"one resumed logic step ages burst once");
    for (unsigned i=0;i<3;i++) { garden_step(&g,0); observe(&g,&s,&f,&fx); }
    CHECK(!s.burst_ticks,"four tick burst expires");
    g.boss.hp=25000; g.tick++; garden_scene_observe(&s,&g);
    CHECK(s.low_hp && s.stats.half_hp_transitions==1,"half HP changes presentation only");
    g.boss.hp=26000; g.tick++; garden_scene_observe(&s,&g);
    CHECK(!s.low_hp && s.stats.half_hp_transitions==2,"half HP transition is not called an attack phase");
    g.boss_attack_shots[0]++; g.tick+=10; garden_scene_observe(&s,&g);
    CHECK(!s.burst_ticks && s.stats.attack_shots_observed==2,"observation gap counts trigger but cannot date fresh burst");
    g.boss_attack_shots[0]++; g.tick++; garden_scene_observe(&s,&g);
    CHECK(s.burst_ticks==4,"single step shot observed");
    garden_step(&g,GARDEN_INPUT_RESTART); garden_scene_observe(&s,&g);
    CHECK(!s.burst_ticks && !s.charge_countdown && !s.low_hp,"restart clears stale Boss cues");
    start(&g,&s,&f,&fx); g.boss.recycle=1; observe(&g,&s,&f,&fx);
    garden_step(&g,0); garden_scene_observe(&s,&g); g.boss.active=0; g.tick++;
    garden_scene_observe(&s,&g); CHECK(!s.burst_ticks && !s.charge_countdown,"inactive Boss clears burst");
    start(&g,&s,&f,&fx); observe(&g,&s,&f,&fx);
    for (unsigned i=0;i<1500;i++) { garden_step(&g,0); garden_scene_observe(&s,&g); }
    CHECK(s.stats.attack_shots_observed==shots(&g),"all actual cycle shots observed exactly");
    CHECK(s.stats.attack_changes_observed>=4 && g.boss_attack_shots[0] &&
          g.boss_attack_shots[1] && g.boss_attack_shots[2],"all original three attack patterns remain reachable");
    garden_scene_set_enabled(&s,0); garden_scene_observe(&s,&g);
    CHECK(!s.burst_ticks,"disable clears transient cue");
    unsigned old=s.stats.attack_shots_observed;
    for (unsigned i=0;i<40;i++) { garden_step(&g,0); garden_scene_observe(&s,&g); }
    CHECK(s.stats.attack_shots_observed==old,"disabled observation never accumulates enabled-shot claims");
    garden_scene_set_enabled(&s,1); garden_scene_observe(&s,&g);
    CHECK(s.stats.attack_shots_observed==old && !s.burst_ticks,"reenable snapshots without inventing intervening shots");
    CHECK(garden_scene_stats(0)==0,"null statistics pointer safe");
    garden_scene_reset(0); garden_scene_set_enabled(0,1); garden_scene_observe(0,&g); garden_scene_observe(&s,0);
    puts("PASS: actual attack-cycle counters, same-tick/pause, disable, half-HP visual transitions and restart/gap clearing");
}

static void fill_pool(GardenGame *g,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{
    start(g,s,f,fx);
    g->player=(GardenPlayer){.active=1,.x=320000,.y=400000}; g->demo_mode=1;
    g->boss.recycle=1; g->boss.flash=3;
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++)
        g->enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.hp=300,.flash=3,.x=(int32_t)(i*20000),.y=100000};
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++)
        g->player_bullets[i]=(GardenPlayerBullet){.active=1,.x=(int32_t)(i*20000),.y=200000};
    for (unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)
        g->enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=3,.x=(int32_t)(i%32U)*20000,.y=(int32_t)(i/32U)*30000};
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++)
        g->effects[i]=(GardenEffect){.active=1,.big=1,.age=0,.x=200000,.y=200000};
    observe(g,s,f,fx);
    for (unsigned i=0;i<GARDEN_FEEDBACK_SPARK_MAX;i++)
        f->sparks[i]=(GardenHitSpark){.active=1,.x=320,.y=150,.age=(uint8_t)i};
    fx->current=(GardenFxPoint){320,400,GARDEN_ASSET_PLAYER,1};
    fx->previous[0]=(GardenFxPoint){315,398,GARDEN_ASSET_PLAYER,1};
    fx->previous[1]=(GardenFxPoint){310,396,GARDEN_ASSET_PLAYER,1};
}

static void full_pool_and_bounds(void)
{
    GardenGame g,snapshot; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw essential[240];
    struct { uint32_t before[4]; GardenDraw d[GARDEN_DRAW_MAX]; uint32_t after[4]; } box;
    fill_pool(&g,&s,&f,&fx); snapshot=g;
    GardenFx essential_fx=fx; garden_fx_set_flags(&essential_fx,GARDEN_FX_FLASH);
    unsigned essential_n=garden_fx_build(&essential_fx,&g,essential,240,1);
    CHECK(essential_n==234,"independent legacy FX builder exposes the complete234essential payloads");
    for (unsigned alpha=0;alpha<2;alpha++) for (unsigned capacity=0;capacity<=241;capacity++) {
        memset(&box,0xA5,sizeof(box));
        unsigned n=garden_scene_build(&s,&f,&fx,&g,box.d,capacity,alpha,1);
        unsigned limit=capacity<240?capacity:240;
        CHECK(n<=limit,"every capacity stays within caller and renderer limits");
        for (unsigned i=0;i<4;i++) CHECK(box.before[i]==0xA5A5A5A5U && box.after[i]==0xA5A5A5A5U,"canaries unmodified");
        for (unsigned i=limit;i<240;i++) {
            GardenDraw expected; memset(&expected,0xA5,sizeof(expected));
            CHECK(!memcmp(&box.d[i],&expected,sizeof(expected)),"no write beyond caller capacity");
        }
        CHECK(!memcmp(&g,&snapshot,sizeof(g)),"full-pool builds never mutate core");
        CHECK(s.stats.essential_dropped==(limit<234U ? 234U-limit : 0U),"essential-drop count accurately reports capacity truncation");
        for (unsigned i=0;i<n;i++) CHECK(box.d[i].asset<GARDEN_ASSET_COUNT && box.d[i].alpha<=32,"all admitted IDs and alpha bounded");
        unsigned found=0U;
        for (unsigned i=0;i<n;i++) if (box.d[i].asset<GARDEN_ASSET_FX_RING32) {
            CHECK(found<essential_n,"no duplicate or overwritten essential payload");
            const GardenDraw *expect=&essential[found++];
            CHECK(box.d[i].x==expect->x && box.d[i].y==expect->y &&
                  box.d[i].asset==expect->asset && box.d[i].alpha==expect->alpha,
                  "all admitted essential payloads preserve exact old FX order, flash and coordinates");
        }
        CHECK(found==(limit<234U ? limit:234U),"all essential payloads fit before lower-priority admission");
        if (limit>=234) CHECK(!s.stats.essential_dropped,"full234 essentials are preserved");
        if (limit>=238) CHECK(f.stats.marker==1 && f.stats.sparks==3,"marker and three sparks win above scene extras");
        if (limit==239) CHECK(!s.stats.wings && s.stats.core==1 && !s.stats.charge && !s.stats.backgrounds,"one spare slot keeps reactor without asymmetrical wing");
        if (limit==240) CHECK(s.stats.wings==2 && !s.stats.core && !s.stats.charge && !s.stats.backgrounds && !fx.stats.ghosts && !fx.stats.rings,"paired wings outrank old decor in full pool");
    }
    unsigned before=s.stats.builds;
    CHECK(!garden_scene_build(&s,&f,&fx,&g,0,240,1,1) && s.stats.builds==before+1 && s.stats.essential_dropped==234,"null list acts as zero capacity with accurate stats");
    CHECK(!garden_scene_build(0,&f,&fx,&g,box.d,240,1,1),"null scene safe");
    CHECK(!garden_scene_build(&s,0,&fx,&g,box.d,240,1,1),"null feedback safe");
    CHECK(!garden_scene_build(&s,&f,0,&g,box.d,240,1,1),"null FX safe");
    CHECK(!garden_scene_build(&s,&f,&fx,0,box.d,240,1,1),"null core safe");
    puts("PASS: full234 essentials, marker/spark priority, paired wing admission, capacities0..241, null and canaries");
}

static void painter_order_and_parallax(void)
{
    GardenGame g,snapshot; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240],again[240],four[240],legacy[240];
    start(&g,&s,&f,&fx);
    g.player=(GardenPlayer){.active=1,.x=320000,.y=400000}; g.demo_mode=1; g.boss.recycle=12;
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=320000,.y=250000};
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=3,.x=200000,.y=240000};
    observe(&g,&s,&f,&fx); f.sparks[0]=(GardenHitSpark){.active=1,.x=320,.y=146}; snapshot=g;
    unsigned n=garden_scene_build(&s,&f,&fx,&g,d,240,1,1);
    CHECK(s.stats.backgrounds==8 && s.stats.wings==2 && s.stats.core==1 && s.stats.charge==1,"common scene uses both parallax layers and all mechanical parts");
    unsigned boss=find(d,n,GARDEN_ASSET_BOSS1),core=find(d,n,GARDEN_ASSET_BOSS_CORE);
    CHECK(find(d,n,GARDEN_ASSET_GARDEN_FAR)<boss && find(d,n,GARDEN_ASSET_BOSS_WING_L)<boss,"backdrop and wings behind Boss");
    CHECK(core==boss+1 && core<find(d,n,GARDEN_ASSET_SHOT),"reactor overlays Boss and stays behind player projectiles");
    CHECK(find(d,n,GARDEN_ASSET_HIT_SPARK)>core && find(d,n,GARDEN_ASSET_FOCUS_VULNERABLE)==n-1,"spark and final marker remain in foreground");
    unsigned cue=find(d,n,GARDEN_ASSET_BOSS_CHARGE);
    CHECK(d[cue].y==186 && d[cue].alpha==10,"anticipation starts at visible lower muzzle with low alpha");
    CHECK(count_asset(d,n,GARDEN_ASSET_GARDEN_FAR)==4 && count_asset(d,n,GARDEN_ASSET_GARDEN_STREAK)==4,"four finite motifs per distinct layer");
    unsigned n2=garden_scene_build(&s,&f,&fx,&g,again,240,1,1);
    for (unsigned i=0;i<n;i++) CHECK(d[i].x==again[i].x && d[i].y==again[i].y && d[i].asset==again[i].asset && d[i].alpha==again[i].alpha,"same-tick paused picture is exactly stable");
    CHECK(n==n2 && !memcmp(&g,&snapshot,sizeof(g)),"scene and builder leave RNG/game state untouched");
    g.tick+=4; garden_scene_observe(&s,&g);
    unsigned n4=garden_scene_build(&s,&f,&fx,&g,four,240,1,1);
    unsigned far=find(d,n,GARDEN_ASSET_GARDEN_FAR),near=find(d,n,GARDEN_ASSET_GARDEN_STREAK);
    CHECK(four[find(four,n4,GARDEN_ASSET_GARDEN_FAR)].y-d[far].y==1,"far layer advances1pixel per4logic ticks");
    CHECK(four[find(four,n4,GARDEN_ASSET_GARDEN_STREAK)].y-d[near].y==3,"near layer advances3pixels per4logic ticks");
    for (unsigned tick=0;tick<2200;tick+=7) {
        g.tick=tick; garden_scene_observe(&s,&g);
        unsigned k=garden_scene_build(&s,&f,&fx,&g,four,240,0,1);
        for (unsigned i=0;i<k;i++) if (four[i].asset>=GARDEN_ASSET_BOSS_WING_L)
            CHECK(four[i].alpha==32,"Alpha off keeps every new asset opaque; muted atlas determines brightness");
        for (unsigned i=0;i<8;i++) CHECK(four[i].x>=0 && four[i].x<640 && four[i].y>=-24 && four[i].y<=503,"parallax entry/exit and large modulo positions bounded");
    }
    g.tick=UINT32_MAX; garden_scene_observe(&s,&g);
    n=garden_scene_build(&s,&f,&fx,&g,d,240,1,1);
    CHECK(n<=240,"maximum tick defined and bounded");
    garden_scene_set_enabled(&s,0); garden_scene_observe(&s,&g);
    GardenFeedback fcopy=f; GardenFx fxcopy=fx;
    n=garden_scene_build(&s,&f,&fx,&g,d,240,1,1);
    n2=garden_feedback_build(&fcopy,&fxcopy,&g,legacy,240,1,1);
    CHECK(n==n2 && !s.stats.wings && !s.stats.core && !s.stats.charge && !s.stats.backgrounds,"disabled scene contains only legacy feedback");
    for (unsigned i=0;i<n;i++) CHECK(d[i].x==legacy[i].x && d[i].y==legacy[i].y && d[i].asset==legacy[i].asset && d[i].alpha==legacy[i].alpha,"disabled draw list matches legacy builder exactly");
    puts("PASS: painter order, visible muzzle, actual field coordinates, two distinct scroll speeds, Alpha-off and disabled legacy equivalence");
}

static void old_decor_lose_before_new_shell(void)
{
    GardenGame g; GardenScene s; GardenFeedback f; GardenFx fx; GardenDraw d[240];
    start(&g,&s,&f,&fx); g.boss.recycle=1;
    g.player=(GardenPlayer){.active=1,.x=320000,.y=400000}; g.demo_mode=1;
    g.effects[0]=(GardenEffect){.active=1,.big=1,.age=0,.x=300000,.y=250000};
    observe(&g,&s,&f,&fx);
    fx.current=(GardenFxPoint){320,400,GARDEN_ASSET_PLAYER,1};
    fx.previous[0]=(GardenFxPoint){315,398,GARDEN_ASSET_PLAYER,1};
    fx.previous[1]=(GardenFxPoint){310,396,GARDEN_ASSET_PLAYER,1};
    unsigned n=garden_scene_build(&s,&f,&fx,&g,d,8,1,1);
    CHECK(n==8 && f.stats.marker==1 && s.stats.wings==2 && s.stats.core==1 && s.stats.charge==1,"mechanical shell takes free slots after essentials and marker");
    CHECK(!fx.stats.ghosts && !fx.stats.rings && !s.stats.backgrounds && s.stats.decorative_dropped==11,"old three decorations and eight backdrops are honestly dropped");
    n=garden_scene_build(&s,&f,&fx,&g,d,11,1,1);
    CHECK(n==11 && fx.stats.ghosts==2 && fx.stats.rings==1 && !s.stats.backgrounds,"old decorative FX win above backdrop");
    n=garden_scene_build(&s,&f,&fx,&g,d,13,1,1);
    CHECK(n==13 && s.stats.backgrounds==2 && count_asset(d,n,GARDEN_ASSET_GARDEN_FAR)==1 && count_asset(d,n,GARDEN_ASSET_GARDEN_STREAK)==1,"two residual slots show both layers");
    puts("PASS: mechanical shell before old decor, old decor before scenery, partial budgets retain both layers");
}

static void deterministic_core_trajectory(void)
{
    GardenGame reference,g,snapshot; GardenScene s; GardenFeedback f; GardenFx fx; GardenDraw d[240];
    garden_init(&reference,12345U); g=reference;
    garden_scene_reset(&s); garden_feedback_reset(&f); garden_fx_reset(&fx); observe(&g,&s,&f,&fx);
    unsigned shots_seen=0,charge_builds=0;
    for (unsigned step=0;step<12000;step++) {
        uint16_t input=0;
        if (step==700 || step==710 || step==3900 || step==3911) input=GARDEN_INPUT_PAUSE;
        if (step==3100 || step==6700 || step==10100) input=GARDEN_INPUT_RESTART;
        garden_step(&reference,input); garden_step(&g,input); snapshot=g;
        observe(&g,&s,&f,&fx);
        unsigned n=garden_scene_build(&s,&f,&fx,&g,d,240,1,1);
        CHECK(!memcmp(&reference,&g,sizeof(g)) && !memcmp(&snapshot,&g,sizeof(g)),"all12000 core snapshots, RNG, bullets, score and attack timings bit-identical");
        CHECK(n<=240 && !s.stats.essential_dropped,"real trajectory complete essential draws fit");
        for (unsigned i=0;i<n;i++) CHECK(d[i].asset<GARDEN_ASSET_COUNT && d[i].alpha<=32,"real trajectory descriptors bounded");
        shots_seen+=s.burst_ticks==4; charge_builds+=s.stats.charge_countdown!=0;
    }
    CHECK(shots_seen && charge_builds && s.stats.attack_shots_observed,"real replay actually exercises warnings and fires");
    printf("PASS:12000 complete core snapshots, real charge builds=%u fresh burst samples=%u\n",charge_builds,shots_seen);
}

int main(void)
{
    anticipation_matches_real_core(); observe_epochs_and_actual_cycle(); full_pool_and_bounds();
    painter_order_and_parallax(); old_decor_lose_before_new_shell(); deterministic_core_trajectory();
    printf("PASS: scene host contract checks=%u\n",checks);
    return 0;
}
