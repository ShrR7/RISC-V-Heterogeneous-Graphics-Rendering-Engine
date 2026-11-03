/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual production core/session contracts. No FPGA, serial, Flash or timing
 * claims. Private core entry points are used for deterministic health fixtures;
 * collision, guard expiry, pause/restart and idle restart also use public APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../src/garden_core.c"
#include "garden_session.h"

static GardenGame game;
static unsigned checks,case_checks,cases;
static FILE *trace;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static void done(const char *name)
{printf("PASS case=%s checks=%u\n",name,checks-case_checks);case_checks=checks;cases++;}
static void fixture(void)
{
    garden_init(&game,0x20261102U);game.tick=10U;game.demo_mode=0U;
    game.player.grace=0U;game.player.recycle=UINT16_MAX;
    game.wave_next=GARDEN_WAVE_ENEMIES;
}
static void record(unsigned hit)
{
    fprintf(trace,"%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%u\n",hit,game.tick,
        game.lives,game.mode,game.player.active,game.player.grace,
        game.reward.hits_taken,game.player.x,game.player.y,game.laser.phase,
        game.ordnance.active,game.ordnance.blast_ticks);
}
static void armed(unsigned i)
{
    game.enemy_bullets[i]=(GardenEnemyBullet){.x=game.player.x,.y=game.player.y,
        .active=1U,.type=GARDEN_BULLET_DART,.colour=1U,
        .age=GARDEN_BULLET_ARM_TICKS,.ttl=500U};
}

static void init_restart(void)
{
    garden_init(&game,0x20261102U);
    CHECK(game.lives==10U && GARDEN_PLAYER_MAX_HP==10U);
    CHECK(game.player.active && game.player.grace==80U && !game.player.respawning);
    game.lives=1U;game.demo_mode=0U;game.paused=1U;game.score=123U;
    game.reward.high_score=500U;
    garden_step(&game,GARDEN_INPUT_RESTART);
    CHECK(game.lives==10U && game.player.active && game.mode==GARDEN_MODE_WAVES);
    CHECK(!game.paused && !game.demo_mode && !game.tick && !game.score);
    CHECK(game.player.grace==80U && game.reward.high_score==500U && game.reward.bombs==2U);
    done("initial_and_paused_restart_HP10");
}

static void ten_hits(void)
{
    fixture();game.mode=GARDEN_MODE_BOSS;
    game.laser.phase=GARDEN_LASER_FIRE;game.laser.ticks=20U;
    game.ordnance=(GardenBomb){.active=1U,.blast_ticks=11U};
    int32_t x=game.player.x,y=game.player.y;
    record(0U);
    for(unsigned hit=1U;hit<=10U;hit++){
        game.player.grace=0U;damage_player(&game);record(hit);
        CHECK(game.lives==10U-hit && game.reward.hits_taken==hit);
        CHECK(game.player.x==x && game.player.y==y && !game.player.respawning);
        if(hit<10U){
            CHECK(game.mode==GARDEN_MODE_BOSS && game.player.active);
            CHECK(game.player.grace==40U);
            CHECK(game.laser.phase==GARDEN_LASER_FIRE && game.laser.ticks==20U);
            CHECK(game.ordnance.active && game.ordnance.blast_ticks==11U);
            for(unsigned i=0;i<GARDEN_MAX_EFFECTS;i++)CHECK(!game.effects[i].active || !game.effects[i].big);
        }else{
            CHECK(game.mode==GARDEN_MODE_GAMEOVER && !game.player.active && !game.end_ticks);
            CHECK(game.laser.phase==GARDEN_LASER_OFF && !game.laser.ticks);
            CHECK(!game.ordnance.active && !game.ordnance.blast_ticks);
        }
    }
    damage_player(&game);CHECK(!game.lives && game.reward.hits_taken==10U);
    done("nine_nonfatal_hits_then_zero_HP_only_GAMEOVER");
}

static void protected_boundaries(void)
{
    fixture();damage_player(&game);CHECK(game.lives==9U && game.player.grace==40U);
    for(unsigned i=1U;i<40U;i++){
        armed(0U);garden_step(&game,0U);
        CHECK(game.lives==9U && game.player.grace==40U-i && game.reward.hits_taken==1U);
    }
    armed(0U);garden_step(&game,0U);
    CHECK(game.lives==8U && game.player.grace==40U && game.reward.hits_taken==2U);
    fixture();game.player.grace=80U;
    for(unsigned i=1U;i<80U;i++){armed(0U);garden_step(&game,0U);CHECK(game.lives==10U);}
    armed(0U);garden_step(&game,0U);CHECK(game.lives==9U);
    fixture();damage_player(&game);garden_step(&game,GARDEN_INPUT_PAUSE);
    uint32_t tick=game.tick;
    for(unsigned i=0;i<100U;i++){armed(0U);garden_step(&game,0U);}
    CHECK(game.paused && game.tick==tick && game.lives==9U && game.player.grace==40U);
    garden_step(&game,GARDEN_INPUT_PAUSE);
    CHECK(!game.paused && game.lives==9U && game.player.grace==39U);
    done("40tick_hit_guard_80tick_start_guard_pause_and_expiry");
}

static void collision_sources(void)
{
    fixture();armed(0U);armed(1U);
    game.boss=(GardenBoss){.active=1U,.x=game.player.x,.y=game.player.y};
    game.laser=(GardenLaser){.phase=GARDEN_LASER_FIRE,.ticks=20U,
        .x={game.player.x,game.player.x},.y=100000};
    check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_FIRE);
    CHECK(game.lives==9U && game.reward.hits_taken==1U && !game.enemy_bullets[0].active && game.enemy_bullets[1].active);
    check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_FIRE);
    CHECK(game.lives==9U && game.reward.hits_taken==1U);
    for(unsigned body=0;body<2U;body++){
        fixture();
        if(body)game.boss=(GardenBoss){.active=1U,.x=game.player.x,.y=game.player.y};
        else game.enemies[0]=(GardenEnemy){.active=1U,.type=GARDEN_ENEMY_DARTER,.x=game.player.x,.y=game.player.y};
        check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_OFF);
        CHECK(game.lives==9U && game.player.active && game.player.grace==40U);
    }
    fixture();
    game.laser=(GardenLaser){.phase=GARDEN_LASER_FIRE,.ticks=20U,
        .x={game.player.x,game.player.x+100000},.y=100000};
    check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_FIRE);
    CHECK(game.lives==9U && game.weapon_stats.laser_hits==1U && game.laser.phase==GARDEN_LASER_FIRE);
    check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_FIRE);
    CHECK(game.lives==9U && game.weapon_stats.laser_hits==1U);
    fixture();armed(0U);game.enemy_bullets[0].age=0U;
    check_player_collisions(&game,game.player.x,game.player.y,GARDEN_LASER_OFF);
    CHECK(game.lives==10U && !game.reward.hits_taken && game.hazard.forming_contacts==1U);
    done("armed_bullet_enemy_boss_laser_one_damage_and_forming_rule");
}

static void heal_boundaries(void)
{
    for(unsigned hp=1U;hp<=10U;hp++){
        fixture();game.lives=(uint8_t)hp;
        drop_pickup(&game,game.player.x,game.player.y,GARDEN_PICKUP_HEAL);run_pickups(&game);
        CHECK(game.lives==(hp<10U ? hp+1U:10U));
        CHECK(game.weapon_stats.heals_spawned==1U && game.weapon_stats.heals_collected==1U);
        CHECK(game.weapon_stats.heals_restored==(hp<10U));
        CHECK(game.reward.items_collected==1U && game.score==GARDEN_PICKUP_SCORE && !game.pickups[0].active);
        for(unsigned i=0;i<5;i++)run_pickups(&game);
        CHECK(game.lives==(hp<10U ? hp+1U:10U) && game.weapon_stats.heals_collected==1U);
    }
    fixture();game.lives=9U;
    for(unsigned i=0;i<8;i++)drop_pickup(&game,game.player.x,game.player.y,GARDEN_PICKUP_HEAL);
    run_pickups(&game);
    CHECK(game.lives==10U && game.weapon_stats.heals_restored==1U && game.weapon_stats.heals_collected==8U);
    fixture();game.lives=1U;damage_player(&game);
    drop_pickup(&game,game.player.x,game.player.y,GARDEN_PICKUP_HEAL);
    for(unsigned i=0;i<100;i++)run_pickups(&game);
    CHECK(!game.lives && !game.player.active && game.mode==GARDEN_MODE_GAMEOVER);
    CHECK(!game.weapon_stats.heals_collected && !game.weapon_stats.heals_restored);
    done("medical_HP1_to10_including3_to4_full_cap_and_no_resurrection");
}

static void actual_drops(void)
{
    for(unsigned large=0;large<2;large++){
        fixture();game.lives=9U;
        game.enemies[0]=(GardenEnemy){.active=1U,.x=320000,.y=100000,
            .hp=600,.type=large ? GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER};
        GardenPlayerBullet bullet={.x=320000,.y=100000,.damage=600U};
        CHECK(hit_target(&game,&bullet,0U));
        CHECK(game.pickups[0].active && game.pickups[0].kind==(large ? GARDEN_PICKUP_HEAL:GARDEN_PICKUP_CORE));
        CHECK(game.weapon_stats.heals_spawned==large);
        game.player.x=game.pickups[0].x;game.player.y=game.pickups[0].y;
        run_pickups(&game);CHECK(game.lives==9U+large);
    }
    done("large_enemy_medical_drop_small_enemy_score_drop_unchanged");
}

static void session_after_zero(void)
{
    GardenSession session;
    fixture();game.lives=1U;
    garden_session_reset(&session,&game,100U,0U);damage_player(&game);
    CHECK(garden_session_update(&session,&game,110U,0U,0U)==0U);
    CHECK(garden_session_update(&session,&game,15109U,0U,0U)==0U);
    uint16_t event=garden_session_update(&session,&game,15110U,0U,0U);
    CHECK(event==GARDEN_INPUT_RESTART && game.demo_mode);
    garden_step(&game,event);
    CHECK(game.lives==10U && game.player.active && game.mode==GARDEN_MODE_WAVES && game.demo_mode);
    done("zero_HP_real15second_idle_restart_restores10_HP");
}

int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"usage: %s damage_trace.csv\n",argv[0]);return 2;}
    trace=fopen(argv[1],"wb");CHECK(trace);
    fputs("hit,tick,hp,mode,active,grace,hits_taken,x,y,laser_phase,bomb_active,blast_ticks\n",trace);
    init_restart();ten_hits();protected_boundaries();collision_sources();heal_boundaries();actual_drops();session_after_zero();
    fclose(trace);
    printf("PASS health10_core cases=%u checks=%u game_bytes=%u\n",cases,checks,(unsigned)sizeof game);
    return 0;
}
