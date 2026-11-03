/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real production core/scene-observer tests. No MMIO, no board claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_scene.h"

static unsigned checks;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)

static unsigned shots(const GardenGame *g)
{ return g->boss_attack_shots[0]+g->boss_attack_shots[1]+g->boss_attack_shots[2]; }

static void fixture(GardenGame *g,unsigned attack,unsigned band)
{
    garden_init(g,12345U); g->tick=100U; g->mode=GARDEN_MODE_BOSS; g->demo_mode=0U;
    g->player.active=0U;
    g->boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.max_hp=50000,
        .hp=band==0U ? 50000 : band==1U ? 35000 : 20000,.attack=(uint8_t)attack,
        .rhythm_state=GARDEN_BOSS_TRANSIT};
}

static void forecast(const GardenGame *g,GardenScene *scene)
{
    GardenGame future=*g;
    unsigned countdown=garden_boss_fire_countdown(g),old=shots(g);
    garden_scene_observe(scene,g);
    CHECK(scene->charge_countdown==countdown,"scene forecasts actual core charge, never legacy recycle");
    if (!countdown) return;
    for (unsigned i=1U;i<=countdown;i++) {
        garden_step(&future,0U);
        CHECK(shots(&future)==old+(i==countdown),"announced charge ends exactly at true first pulse");
    }
}

static void schedule_contract(void)
{
    for (unsigned attack=1U;attack<=3U;attack++) for (unsigned band=0U;band<3U;band++) {
        GardenGame g; GardenScene scene; fixture(&g,attack,band); garden_scene_reset(&scene);
        unsigned charge=20U-band*4U,gap=attack==2U ? 6U-band : 12U-band*2U;
        unsigned pulses=attack==2U ? 6U : 2U,size=attack==1U ? 64U : attack==2U ? 9U : 36U;
        garden_step(&g,0U);
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==charge,"complete20/16/12tick anticipation begins before fire");
        CHECK(g.boss.hp_band==band && g.boss.burst_left==pulses,"HP band and finite pulse budget latched");
        unsigned emitted=0U,previous_fire=0U;
        for (unsigned tick=1U;tick<=charge+(pulses-1U)*gap;tick++) {
            forecast(&g,&scene); unsigned before=shots(&g);
            garden_step(&g,0U); garden_scene_observe(&scene,&g);
            if (shots(&g)!=before) {
                CHECK(shots(&g)==before+1U,"one generator call per scheduled pulse");
                CHECK(!emitted ? tick==charge : tick-previous_fire==gap,"exact precharge and short pulse intervals");
                CHECK(scene.burst_ticks==GARDEN_SCENE_BURST_TICKS,"observed real pulse drives release cue");
                previous_fire=tick; emitted++;
                CHECK(g.enemy_bullet_count==emitted*size,"unchanged generator emits its full64/9/36projectiles");
            }
            CHECK(!g.dropped_enemy_bullets && g.enemy_bullet_count<=GARDEN_BOSS_BULLET_BUDGET,"whole train admits within128live budget without pool drop");
        }
        CHECK(emitted==pulses && g.rhythm.shots_admitted==pulses && g.rhythm.bursts_completed==1U,"finite burst completes exactly once");
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_REST && g.boss.rhythm_ticks==80U-band*16U,"recovery80/64/48ticks starts after last pulse");
        unsigned before=shots(&g),rest=g.boss.rhythm_ticks;
        for (unsigned i=0U;i<rest;i++) { garden_step(&g,0U); CHECK(shots(&g)==before,"no attacks during recovery"); }
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_TRANSIT && g.boss.move_ticks==40U,"recovery then original40tick relocation");
        CHECK(g.boss.attack==(attack==3U ? 1U : attack+1U),"all three original generators retain deterministic ordering");
        for (unsigned i=0U;i<40U;i++) { garden_step(&g,0U); CHECK(shots(&g)==before,"no attacks while relocating"); }
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE || g.boss.rhythm_state==GARDEN_BOSS_WAIT,"transit starts true charge or capacity wait");
    }
    puts("PASS: all3patterns x3HPbands, exact charge20/16/12, pulse gaps12/10/8or6/5/4, recovery80/64/48, transit40");
}

static void fill_pool(GardenGame *g)
{
    for (unsigned i=0U;i<192U;i++) g->enemy_bullets[i]=(GardenEnemyBullet){
        .active=1U,.type=GARDEN_BULLET_DART,.x=320000,.y=200000,.ttl=500,.cord_wait=300};
}

static void capacity_and_pause(void)
{
    GardenGame g,saved; GardenScene scene,ss;
    fixture(&g,1U,0U); fill_pool(&g); garden_scene_reset(&scene);
    garden_step(&g,0U);
    CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT && !garden_boss_fire_countdown(&g),"full pool waits without announcing impossible shot");
    uint32_t rng=g.rng;
    for (unsigned i=0U;i<5U;i++) garden_step(&g,0U);
    CHECK(!shots(&g) && !g.dropped_enemy_bullets && g.rng==rng && g.rhythm.capacity_wait_ticks==5U,"capacity wait preserves RNG and counts actual wait ticks");
    memset(g.enemy_bullets,0,sizeof(g.enemy_bullets)); garden_step(&g,0U);
    CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==20U,"capacity recovery starts full new anticipation");
    forecast(&g,&scene); saved=g; ss=scene;
    garden_step(&g,GARDEN_INPUT_PAUSE); garden_scene_observe(&scene,&g);
    CHECK(g.tick==saved.tick && !memcmp(&g.rhythm,&saved.rhythm,sizeof(g.rhythm)) &&
          !memcmp(&g.boss,&saved.boss,sizeof(g.boss)),"pause freezes actual Boss and rhythm telemetry");
    CHECK(!memcmp(&scene,&ss,sizeof(scene)),"pause freezes visual prediction");
    garden_step(&g,GARDEN_INPUT_PAUSE);
    for (unsigned i=1U;i<20U;i++) garden_step(&g,0U);
    CHECK(shots(&g)==1U && g.boss.rhythm_state==GARDEN_BOSS_BURST,"capacity-recovered shot follows complete charge");
    fill_pool(&g);
    for (unsigned i=0U;i<12U;i++) garden_step(&g,0U);
    CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT && g.boss.burst_left==1U &&
          g.rhythm.shots_deferred==1U && !g.dropped_enemy_bullets,"adversarial competing producer cannot truncate a pulse");
    memset(g.enemy_bullets,0,sizeof(g.enemy_bullets)); garden_step(&g,0U);
    for (unsigned i=0U;i<20U;i++) garden_step(&g,0U);
    CHECK(shots(&g)==2U && g.rhythm.bursts_completed==1U,"backpressured remaining pulse receives new warning and completes");
    garden_step(&g,GARDEN_INPUT_RESTART); garden_scene_observe(&scene,&g);
    GardenRhythmStats zero={0}; CHECK(!memcmp(&g.rhythm,&zero,sizeof(zero)) &&
        !scene.charge_countdown && !scene.burst_ticks && g.tick==0U,"restart clears actual and visual rhythm epochs");
    CHECK(!garden_boss_fire_countdown(0),"null forecast safe");
    puts("PASS: whole-train admission, adversarial midtrain backpressure, RNG, complete resumed warning, pause/restart");
}

static void rendering_budget_boundaries(void)
{
    CHECK(GARDEN_MAX_ENEMY_BULLETS==192U && GARDEN_BOSS_BULLET_BUDGET==128U,
          "live rendering budget is128 while the unchanged physical pool remains192");
    for (unsigned attack=1U;attack<=3U;attack++) for (unsigned blocked=0U;blocked<2U;blocked++) {
        GardenGame g; fixture(&g,attack,0U);
        unsigned size=attack==1U ? 64U : attack==2U ? 9U : 36U;
        unsigned pulses=attack==2U ? 6U : 2U;
        unsigned existing=128U-size*pulses+blocked;
        for (unsigned i=0U;i<existing;i++) g.enemy_bullets[i]=(GardenEnemyBullet){
            .active=1U,.type=GARDEN_BULLET_DART,.x=320000,.y=200000,.ttl=500,.cord_wait=300};
        /* Deliberately stale count: admission must inspect the actual slots. */
        g.enemy_bullet_count=192U;
        uint32_t rng=g.rng; garden_step(&g,0U);
        CHECK(g.boss.rhythm_state==(blocked ? GARDEN_BOSS_WAIT : GARDEN_BOSS_CHARGE),
              "one live projectile over entire-train boundary waits despite spare physical slots");
        CHECK(!shots(&g) && g.rng==rng && !g.dropped_enemy_bullets,
              "budget admission never spawns partial geometry, consumes RNG or drops bullets");
        if (blocked) {
            CHECK(!garden_boss_fire_countdown(&g),"render-budget WAIT does not invent next-shot forecast");
            g.enemy_bullets[existing-1U].active=0U; garden_step(&g,0U);
            CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==20U,
                  "one actual freed slot starts a complete new20tick warning at exact budget boundary");
        }
        for (unsigned i=0U;i<20U+(pulses-1U)*(attack==2U ? 6U : 12U);i++) garden_step(&g,0U);
        CHECK(shots(&g)==pulses && g.enemy_bullet_count==128U && !g.dropped_enemy_bullets,
              "all three complete generator trains exactly fill128 without reducing geometry");
    }
    puts("PASS:128live/192physical budget, all3whole-train exact boundaries, stale counts, fresh warning on capacity recovery");
}

static void hp_thresholds_and_latched_train(void)
{
    static const int32_t hp[]={50000,35001,35000,20001,20000,1};
    static const unsigned expected[]={0U,0U,1U,1U,2U,2U};
    for (unsigned i=0U;i<6U;i++) {
        GardenGame g; fixture(&g,1U,0U); g.boss.hp=hp[i]; garden_step(&g,0U);
        CHECK(g.boss.hp_band==expected[i] && g.boss.charge_ticks==20U-4U*expected[i],"70%/40%boundaries exact and inclusive");
        g.boss.hp=1;
        unsigned old_charge=g.boss.charge_ticks,old_gap=g.boss.burst_gap,old_rest=g.boss.rest_ticks;
        for (unsigned t=0U;t<old_charge+old_gap;t++) garden_step(&g,0U);
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_REST && g.boss.rhythm_ticks==old_rest,
              "HP change during charge never truncates the latched pulse train or recovery");
        for (unsigned t=0U;t<old_rest+40U;t++) garden_step(&g,0U);
        CHECK(g.boss.hp_band==2U && g.boss.charge_ticks==12U && g.boss.rest_ticks==48U,
              "new HP band takes effect at next charge boundary");
    }
    puts("PASS: exact70%/40%HP boundaries and stable current train before next-band adoption");
}

static void vulnerable_replay(void)
{
    unsigned wins=0U,losses=0U,restarts=0U,peak=0U,seed12345_score=0U,seed12345_tick=0U;
    uint32_t total_hits=0U,total_kills=0U,shot_counts[3]={0},waits=0U,observed_drops=0U;
    for (unsigned trial=0U;trial<65U;trial++) {
        unsigned seed=trial ? trial : 12345U;
        GardenGame g; GardenScene scene; garden_init(&g,seed); garden_scene_reset(&scene);
        unsigned ends=0U,trial_wins=0U;
        for (unsigned step=0U;step<(trial ? 12000U : 24000U);step++) {
            unsigned old_mode=g.mode,old_tick=g.tick,old_end_ticks=g.end_ticks; uint32_t score=g.score;
            garden_step(&g,0U); garden_scene_observe(&scene,&g);
            CHECK(g.enemy_bullet_count<=GARDEN_BOSS_BULLET_BUDGET && g.player_bullet_count<=24U && g.enemy_count<=8U,"whole real scene obeys128enemy-bullet budget and physical pool bounds");
            CHECK(g.lives<=3U && g.combat.count<=24U && !g.combat.dropped_total,"ordinary lives and full hit event budget");
            uint32_t credited=0U;
            for (unsigned i=0U;i<g.combat.count;i++) {
                const GardenHitEvent *e=&g.combat.items[i];
                CHECK(e->damage>0U && e->damage<=150U,"real effective damage excludes zero and overkill");
                CHECK(e->score_delta==1U+e->kill_reward,"actual positive hits+1 retain20/80/1000killaward");
                credited+=e->score_delta;
            }
            if (g.tick>old_tick) CHECK(g.score-score==credited,"score growth uses real contact events only");
            else CHECK(g.tick==0U && old_end_ticks==159U && (old_mode==GARDEN_MODE_GAMEOVER || old_mode==GARDEN_MODE_VICTORY),"automatic restart follows exactly160end-state steps");
            if (g.enemy_bullet_peak>peak) peak=g.enemy_bullet_peak;
            if (g.mode!=old_mode && (g.mode==GARDEN_MODE_GAMEOVER || g.mode==GARDEN_MODE_VICTORY)) {
                ends++; trial_wins+=g.mode==GARDEN_MODE_VICTORY;
                wins+=g.mode==GARDEN_MODE_VICTORY; losses+=g.mode==GARDEN_MODE_GAMEOVER;
                total_hits+=g.combat.hits_total; total_kills+=g.combat.kills_total;
                for (unsigned i=0U;i<3U;i++) shot_counts[i]+=g.boss_attack_shots[i];
                waits+=g.rhythm.capacity_wait_ticks; observed_drops+=g.dropped_enemy_bullets;
                if (!trial && g.mode==GARDEN_MODE_VICTORY) {
                    seed12345_score=g.score; seed12345_tick=g.tick;
                    printf("SEED12345: cycle=%u victory_tick=%u score=%u lives=%u peak=%u waits=%u shots=%u/%u/%u charge_windows=%u bursts=%u restart_tick=%u\n",
                        ends,g.tick,g.score,g.lives,g.enemy_bullet_peak,g.rhythm.capacity_wait_ticks,
                        g.boss_attack_shots[0],g.boss_attack_shots[1],g.boss_attack_shots[2],
                        g.rhythm.charge_windows,g.rhythm.bursts_completed,g.tick+160U);
                }
            }
            if (g.tick<old_tick) {
                restarts++;
                if (trial || ends>=3U) break;
            }
        }
        CHECK(ends>0U,"every tested seed completes vulnerable win or loss");
        if (!trial) CHECK(ends>=3U && trial_wins==ends && seed12345_score>1320U,"demonstration seed reaches three real vulnerable victories, not one victory among losses");
    }
    CHECK(peak<=GARDEN_BOSS_BULLET_BUDGET && !observed_drops && shot_counts[0] && shot_counts[1] && shot_counts[2],"all original attack generators observed, peak<=128 and no actual projectile truncation");
    printf("PASS: real vulnerable replay seeds=65 wins=%u losses=%u restarts=%u peak=%u drops=%u waits=%u hits=%u kills=%u shots=%u/%u/%u seed12345_score=%u seed12345_victory_tick=%u\n",
        wins,losses,restarts,peak,observed_drops,waits,total_hits,total_kills,shot_counts[0],shot_counts[1],shot_counts[2],seed12345_score,seed12345_tick);
}

int main(void)
{
    schedule_contract(); capacity_and_pause(); rendering_budget_boundaries(); hp_thresholds_and_latched_train(); vulnerable_replay();
    printf("PASS: rhythm core checks=%u sizeof_game=%u sizeof_boss=%u\n",checks,(unsigned)sizeof(GardenGame),(unsigned)sizeof(GardenBoss));
    return 0;
}
