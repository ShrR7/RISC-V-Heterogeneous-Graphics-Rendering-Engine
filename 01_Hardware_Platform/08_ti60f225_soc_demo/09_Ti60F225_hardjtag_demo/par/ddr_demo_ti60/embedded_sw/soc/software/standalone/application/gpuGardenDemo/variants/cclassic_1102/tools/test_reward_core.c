/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reward gameplay tests against the real production core, without MMIO.
 * Including the translation unit permits focused allocation/contact checks;
 * lifecycle, input, collision and complete runs use the public step API.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"

static unsigned checks,runs,wins,max_live;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); exit(1); } } while (0)

static GardenGame fixture(void)
{
    GardenGame g; garden_init(&g,12345U); g.tick=10U;
    g.demo_mode=0U; g.player.grace=0U; g.player.recycle=200U;
    return g;
}

static void bullet(GardenGame *g,unsigned slot,int dx,int dy)
{
    g->enemy_bullets[slot]=(GardenEnemyBullet){.active=1U,
        .type=GARDEN_BULLET_DIAMOND,.x=g->player.x+dx,.y=g->player.y+dy,
        .ttl=500U,.cord_wait=-1};
}

static void graze_contract(void)
{
    const int offsets[][2]={{28000,0},{-28000,0},{0,28000},{0,-28000},
        {28000,28000},{28001,0},{0,-28001},{9000,9000},{9001,0}};
    for (unsigned i=0U;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        GardenGame g=fixture(); unsigned should_graze=i<5U || i==8U;
        bullet(&g,0U,offsets[i][0],offsets[i][1]);
        garden_step(&g,0U);
        CHECK(g.reward.graze_count==should_graze);
        CHECK(g.score==should_graze*GARDEN_GRAZE_SCORE);
        CHECK(g.reward.high_score==g.score);
        if (i==7U) CHECK(g.reward.hits_taken==1U && g.lives==2U);
        for (unsigned n=0U;n<10U;n++) garden_step(&g,0U);
        CHECK(g.reward.graze_count==should_graze);
    }
    GardenGame g=fixture(); bullet(&g,0U,20000,0);
    g.player.grace=10U; garden_step(&g,0U);
    CHECK(!g.reward.graze_count && !g.enemy_bullets[0].grazed);
    g.player.grace=0U; g.player.respawning=10U; garden_step(&g,0U);
    CHECK(!g.reward.graze_count);
    g.player.respawning=0U; g.player.y=410000;
    g.enemy_bullets[0].cord_wait=20; garden_step(&g,0U);
    CHECK(!g.reward.graze_count && !g.enemy_bullets[0].grazed);
    g.enemy_bullets[0].cord_wait=-1; garden_step(&g,0U);
    CHECK(g.reward.graze_count==1U);
    GardenGame damaged=fixture(); bullet(&damaged,0U,0,0); bullet(&damaged,1U,20000,0);
    garden_step(&damaged,0U);
    CHECK(damaged.reward.hits_taken==1U && !damaged.reward.graze_count);
    /* Reusing a slot really resets one-graze lifetime state. */
    g.enemy_bullets[0].active=0U;
    GardenEnemyBullet *b=new_bullet(&g,GARDEN_BULLET_DIAMOND,
                                  g.player.x+20000,g.player.y,0,1,-1);
    CHECK(b==&g.enemy_bullets[0] && !b->grazed);
    garden_step(&g,0U); CHECK(g.reward.graze_count==2U);
    g.score=UINT32_MAX-2U; g.reward.high_score=0U;
    bullet(&g,1U,25000,0); garden_step(&g,0U);
    CHECK(g.score==UINT32_MAX && g.reward.high_score==UINT32_MAX);
    CHECK(g.reward.graze_score_total==12U);
    puts("PASS graze: exact square/contact boundaries, activation, invulnerability, respawn, once-per-slot-lifetime, saturation");
}

static void pickup_contract(void)
{
    GardenGame g=fixture(); drop_pickup(&g,100000,100000);
    CHECK(g.reward.items_spawned==1U && g.pickups[0].active);
    int32_t old_y=g.pickups[0].y; run_pickups(&g);
    CHECK(g.pickups[0].y<old_y && !g.pickups[0].magnet);
    for (unsigned i=1U;i<20U;i++) run_pickups(&g);
    CHECK(g.pickups[0].y>old_y && !g.pickups[0].magnet);
    CHECK(!g.reward.items_collected && !g.score);
    g.demo_mode=1U; g.mode=GARDEN_MODE_VICTORY;
    int32_t old_distance=abs32(g.pickups[0].x-g.player.x)+abs32(g.pickups[0].y-g.player.y);
    run_pickups(&g);
    CHECK(g.pickups[0].magnet && g.pickups[0].active && !g.reward.items_collected);
    CHECK(abs32(g.pickups[0].x-g.player.x)+abs32(g.pickups[0].y-g.player.y)<old_distance);
    for (unsigned i=0U;i<100U && g.pickups[0].active;i++) run_pickups(&g);
    CHECK(g.reward.items_collected==1U && !g.pickups[0].active);
    CHECK(g.score==GARDEN_PICKUP_SCORE && g.reward.item_score_total==GARDEN_PICKUP_SCORE);
    CHECK(g.reward.pickup_flash_ticks==16U && !g.reward.pickup_count);
    CHECK(g.reward.pickup_flash_x==g.player.x && g.reward.pickup_flash_y==g.player.y);
    for (unsigned i=0U;i<30U;i++) run_pickups(&g);
    CHECK(g.reward.items_collected==1U && g.score==GARDEN_PICKUP_SCORE);

    g=fixture(); drop_pickup(&g,g.player.x+96001,g.player.y);
    g.pickups[0].age=20U; run_pickups(&g); CHECK(!g.pickups[0].magnet);
    g.pickups[0].x=g.player.x+96000; run_pickups(&g); CHECK(g.pickups[0].magnet);
    g=fixture(); g.player.active=0U;
    for (unsigned i=0U;i<GARDEN_MAX_PICKUPS+1U;i++) drop_pickup(&g,200000,200000);
    CHECK(g.reward.items_spawned==GARDEN_MAX_PICKUPS && g.reward.items_dropped==1U);
    for (unsigned i=0U;i<481U;i++) run_pickups(&g);
    CHECK(!g.reward.pickup_count && !g.reward.items_collected && !g.score);
    g=fixture(); drop_pickup(&g,g.player.x,g.player.y); g.player.respawning=3U;
    run_pickups(&g); CHECK(!g.reward.items_collected);
    g.player.respawning=0U; g.score=UINT32_MAX-4U; run_pickups(&g);
    CHECK(g.score==UINT32_MAX && g.reward.item_score_total==4U);
    puts("PASS pickups: real pop/fall/attraction/contact, exact96px magnet boundary, fixed-pool exhaustion/TTL, respawn exclusion, credited-score saturation");
}

static void bomb_contract(void)
{
    GardenGame g=fixture();
    for (unsigned i=0U;i<60U;i++) bullet(&g,i,100000,0);
    bullet(&g,60U,-100000,0); g.enemy_bullets[60].cord_wait=100;
    g.enemy_bullet_count=60U;
    garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(g.reward.bombs==1U && g.reward.bombs_used==1U);
    CHECK(g.reward.bullets_cleared==61U && !g.enemy_bullet_count);
    CHECK(g.player.grace==40U && g.reward.bomb_flash_ticks==12U);
    CHECK(!g.score && !g.reward.graze_count);
    garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(!g.reward.bombs && g.reward.bombs_used==2U);
    garden_step(&g,GARDEN_INPUT_BOMB); CHECK(g.reward.bombs_used==2U);
    for (unsigned mode=GARDEN_MODE_VICTORY;mode<=GARDEN_MODE_GAMEOVER;mode++) {
        g=fixture(); g.mode=(uint8_t)mode; garden_step(&g,GARDEN_INPUT_BOMB);
        CHECK(g.reward.bombs==2U && !g.reward.bombs_used);
    }
    g=fixture(); g.player.respawning=1U; garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(!g.reward.bombs_used && g.reward.bombs==2U);
    g=fixture(); g.player.active=0U; garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(!g.reward.bombs_used);
    g=fixture(); garden_step(&g,GARDEN_INPUT_PAUSE|GARDEN_INPUT_BOMB);
    CHECK(g.paused && !g.reward.bombs_used && !g.reward.run_ticks);
    g=fixture(); g.player.grace=100U; garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(g.player.grace==99U);

    g=fixture(); g.mode=GARDEN_MODE_BOSS; g.demo_mode=1U;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=1234,.max_hp=50000,
        .rhythm_state=GARDEN_BOSS_REST,.rhythm_ticks=200U};
    for (unsigned i=0U;i<60U;i++) bullet(&g,i,100000,0);
    g.enemy_bullet_count=60U; garden_step(&g,0U);
    CHECK(g.reward.bombs==1U && g.reward.bombs_used==1U && !g.enemy_bullet_count);
    CHECK(g.boss.hp==1234 && g.boss.active);
    for (unsigned i=0U;i<60U;i++) bullet(&g,i,100000,0);
    g.enemy_bullet_count=60U; garden_step(&g,0U);
    CHECK(g.reward.bombs_used==1U && g.enemy_bullet_count==60U);
    puts("PASS bombs: finite2, clear real active bullets,40tick grace, no damage/score, pause/end/dead/respawn rejection, one actual demo use");
}

static void kills_and_end_contract(void)
{
    for (unsigned boss=0U;boss<2U;boss++) {
        GardenGame g=fixture();
        int32_t x=320000,y=100000;
        if (boss) {
            g.mode=GARDEN_MODE_BOSS;
            g.boss=(GardenBoss){.active=1U,.x=x,.y=y,.hp=15,.max_hp=50000,
                .rhythm_state=GARDEN_BOSS_REST,.rhythm_ticks=200};
        } else g.enemies[0]=(GardenEnemy){.active=1U,.type=GARDEN_ENEMY_DARTER,
            .x=x,.y=y,.hp=15,.phase=2U,.target_time=50,.recycle=20};
        g.player_bullets[0]=(GardenPlayerBullet){.active=1U,.x=x,.y=y,.damage=95U};
        garden_step(&g,0U);
        CHECK(g.combat.count==1U && g.combat.items[0].damage==15U);
        CHECK(g.combat.items[0].killed && g.combat.items[0].kill_reward==(boss ? 1000U : 20U));
        CHECK(g.score==(boss ? 1001U : 21U));
        CHECK(g.reward.high_score==g.score && g.reward.items_spawned==1U && g.pickups[0].active);
        if (boss) {
            CHECK(g.mode==GARDEN_MODE_VICTORY && !g.end_ticks && !g.boss.active);
            CHECK(!g.player_bullet_count && !g.enemy_bullet_count && !g.enemy_count);
            for (unsigned i=0U;i<GARDEN_MAX_PLAYER_BULLETS;i++) CHECK(!g.player_bullets[i].active);
            for (unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++) CHECK(!g.enemy_bullets[i].active);
            uint32_t time=g.reward.run_ticks; g.demo_mode=1U;
            for (unsigned tick=1U;tick<GARDEN_END_TICKS;tick++) {
                garden_step(&g,0U);
                CHECK(g.mode==GARDEN_MODE_VICTORY && g.end_ticks==tick);
                CHECK(g.reward.run_ticks==time && !g.combat.count);
            }
            CHECK(g.reward.items_collected==1U && g.score==1026U);
            garden_step(&g,0U);
            CHECK(g.mode==GARDEN_MODE_WAVES && !g.tick && !g.score && g.reward.bombs==2U);
            CHECK(g.reward.high_score==1026U && !g.reward.run_ticks && !g.reward.items_collected);
        }
    }
    GardenGame g=fixture(); g.score=300U; g.reward.high_score=400U;
    garden_step(&g,GARDEN_INPUT_RESTART);
    CHECK(g.reward.high_score==400U && !g.score && g.reward.bombs==2U);
    garden_init(&g,12345U); CHECK(!g.reward.high_score);
    puts("PASS combat/end: effective-HP damage (15, not weak point), unchanged hit+1/kill20/1000, real kill drop,320tick end, frozen elapsed, final pickup, restart-only retained best, cold reset0");
}

static unsigned shots(const GardenGame *g)
{ return g->boss_attack_shots[0]+g->boss_attack_shots[1]+g->boss_attack_shots[2]; }

static void pulse_contract(void)
{
    for (unsigned attack=1U;attack<=3U;attack++) for (unsigned band=0U;band<3U;band++) {
        GardenGame g=fixture(); g.player.active=0U; g.mode=GARDEN_MODE_BOSS;
        g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=band==0U ? 50000 : band==1U ? 35000 : 20000,
            .max_hp=50000,.attack=(uint8_t)attack,.rhythm_state=GARDEN_BOSS_TRANSIT};
        unsigned pulses=attack==1U ? 1U : attack==2U ? 6U : 2U;
        unsigned size=attack==1U ? 64U : attack==2U ? 9U : 36U;
        garden_step(&g,0U);
        CHECK(g.boss.burst_left==pulses && g.boss.hp_band==band);
        CHECK(g.boss.rhythm_ticks==20U-band*4U);
        unsigned emitted=0U;
        while (g.boss.rhythm_state!=GARDEN_BOSS_REST) {
            unsigned old=shots(&g); garden_step(&g,0U);
            if (shots(&g)>old) ++emitted;
            CHECK(g.enemy_bullet_count<=80U && !g.dropped_enemy_bullets);
        }
        CHECK(emitted==pulses && g.enemy_bullet_count==pulses*size);
        CHECK(g.boss.rhythm_ticks==80U-band*16U);
    }
    GardenGame g=fixture(); g.player.active=0U; g.mode=GARDEN_MODE_BOSS;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.attack=1U,
        .rhythm_state=GARDEN_BOSS_TRANSIT};
    for (unsigned i=0U;i<17U;i++) bullet(&g,i,100000,0);
    garden_step(&g,0U); CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT && !shots(&g));
    memset(g.enemy_bullets,0,sizeof(g.enemy_bullets)); garden_step(&g,0U);
    CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==20U);
    puts("PASS budget: complete64/54/72 trains at80live, all3patterns x70/40HP bands, no half-ring, insufficient capacity waits then full charge");
}

static void full_runs(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    for (unsigned s=0U;s<sizeof(seeds)/sizeof(seeds[0]);s++) {
        GardenGame g; garden_init(&g,seeds[s]); unsigned victory=0U,terminal=0U;
        for (unsigned n=0U;n<10000U;n++) {
            uint32_t old_tick=g.tick;
            garden_step(&g,0U);
            if (g.enemy_bullet_count>max_live) max_live=g.enemy_bullet_count;
            CHECK(g.enemy_bullet_count<=GARDEN_BOSS_BULLET_BUDGET);
            CHECK(g.reward.pickup_count<=GARDEN_MAX_PICKUPS);
            CHECK(!g.combat.dropped_total && !g.dropped_enemy_bullets);
            CHECK(g.score==g.combat.hit_score_total+g.combat.kill_score_total+
                           g.reward.graze_score_total+g.reward.item_score_total);
            CHECK(g.reward.high_score>=g.score);
            if (g.mode==GARDEN_MODE_VICTORY && !g.end_ticks) {
                victory=terminal=1U; wins++; CHECK(g.reward.items_collected>0U);
                CHECK(g.reward.bombs_used==1U && g.reward.bombs==1U);
                CHECK(g.boss_attack_shots[0] && g.boss_attack_shots[1] && g.boss_attack_shots[2]);
                CHECK(g.rhythm.hp_band_changes>=2U);
                printf("RUN seed=%u victory_tick=%u score=%u graze=%u items=%u hits=%u bombs=%u peak=%u\n",
                    seeds[s],g.tick,g.score,g.reward.graze_count,g.reward.items_collected,
                    g.reward.hits_taken,g.reward.bombs_used,g.enemy_bullet_peak);
            }
            if (g.mode==GARDEN_MODE_GAMEOVER && !g.end_ticks) {
                terminal=1U; CHECK(g.reward.hits_taken==3U && !g.player.active && !g.lives);
                printf("RUN seed=%u gameover_tick=%u score=%u graze=%u items=%u hits=%u bombs=%u peak=%u\n",
                    seeds[s],g.tick,g.score,g.reward.graze_count,g.reward.items_collected,
                    g.reward.hits_taken,g.reward.bombs_used,g.enemy_bullet_peak);
            }
            if (g.tick<old_tick) {
                CHECK(terminal && g.reward.high_score>0U && g.reward.bombs==2U);
                runs++; break;
            }
        }
        CHECK(terminal && runs==s+1U);
        if (!s) CHECK(victory); /* Shipped seed12345 demonstrates a full clear. */
    }
}

int main(void)
{
    graze_contract(); pickup_contract(); bomb_contract(); kills_and_end_contract(); pulse_contract(); full_runs();
    printf("RESULT checks=%u runs=%u wins=%u max_live=%u game_bytes=%u reward_bytes=%u pickup_bytes=%u enemy_bullet_bytes=%u\n",
        checks,runs,wins,max_live,(unsigned)sizeof(GardenGame),(unsigned)sizeof(GardenRewardStats),
        (unsigned)sizeof(GardenPickup),(unsigned)sizeof(GardenEnemyBullet));
    return 0;
}
