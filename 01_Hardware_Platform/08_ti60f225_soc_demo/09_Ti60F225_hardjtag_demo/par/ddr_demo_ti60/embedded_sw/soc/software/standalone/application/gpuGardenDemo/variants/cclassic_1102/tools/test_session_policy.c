/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real wall-clock policy and core death integration, no MMIO or board claims. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_session.h"

static unsigned checks;
static GardenGame g;
#define CHECK(x) do { checks++;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);} }while(0)

static void idle_policy(void)
{
    GardenSession s;
    memset(&g,0,sizeof g);g.mode=GARDEN_MODE_WAVES;
    garden_session_reset(&s,&g,100U,0U);
    CHECK(garden_session_update(&s,&g,10099U,0U,0U)==0U && !g.demo_mode);
    CHECK(garden_session_update(&s,&g,10100U,0U,0U)==0U && g.demo_mode);
    g.paused=1U;
    CHECK(garden_session_update(&s,&g,10101U,1U,10101U)==0U && !g.demo_mode && g.paused);
    CHECK(garden_session_update(&s,&g,20101U,1U,10101U)==0U && g.demo_mode && g.paused);
    g.mode=GARDEN_MODE_BOSS;
    CHECK(garden_session_update(&s,&g,20102U,1U,10101U)==0U && g.demo_mode);
    CHECK(garden_session_update(&s,&g,20103U,2U,20103U)==0U && !g.demo_mode && g.paused);
    puts("PASS 10s real inactivity, resumed input, no neutral heartbeat credit, pause preserved");
}

static void gameover_policy(void)
{
    GardenSession s;
    memset(&g,0,sizeof g);g.mode=GARDEN_MODE_BOSS;g.demo_mode=1U;
    garden_session_reset(&s,&g,0U,0U);
    g.mode=GARDEN_MODE_GAMEOVER;
    CHECK(garden_session_update(&s,&g,100000U,0U,0U)==0U);
    CHECK(garden_session_update(&s,&g,114999U,0U,0U)==0U);
    CHECK(garden_session_update(&s,&g,115000U,1U,115000U)==0U && !g.demo_mode); /* actual input renews idle */
    CHECK(garden_session_update(&s,&g,129999U,1U,115000U)==0U);
    CHECK(garden_session_update(&s,&g,130000U,1U,115000U)==GARDEN_INPUT_RESTART);
    CHECK(garden_session_update(&s,&g,130001U,1U,115000U)==0U); /* exactly once */
    g.mode=GARDEN_MODE_WAVES;g.demo_mode=0U;
    CHECK(garden_session_update(&s,&g,130002U,1U,115000U)==0U);
    CHECK(garden_session_update(&s,&g,140001U,1U,115000U)==0U && !g.demo_mode);
    CHECK(garden_session_update(&s,&g,140002U,1U,115000U)==0U && g.demo_mode);
    g.mode=GARDEN_MODE_GAMEOVER;g.demo_mode=0U;
    garden_session_reset(&s,&g,UINT32_MAX-9999U,0U);
    CHECK(garden_session_update(&s,&g,4999U,0U,0U)==0U);
    CHECK(garden_session_update(&s,&g,5000U,0U,0U)==GARDEN_INPUT_RESTART);
    puts("PASS 15s GAME OVER idle from entry, actual-input reset, one-shot and uint32 wrap");
}

static void real_core_death(void)
{
    garden_init(&g,12345U);g.demo_mode=0U;
    CHECK(g.lives==GARDEN_PLAYER_MAX_HP);
    g.player.grace=0U;g.player.respawning=0U;g.player.active=1U;
    g.enemy_bullets[0]=(GardenEnemyBullet){.x=g.player.x,.y=g.player.y,
        .age=GARDEN_BULLET_ARM_TICKS,.ttl=500U,.active=1U,.type=GARDEN_BULLET_DART,.colour=1U};
    garden_step(&g,0U);
    CHECK(g.mode==GARDEN_MODE_WAVES && g.player.active);
    CHECK(g.reward.hits_taken==1U && g.lives==GARDEN_PLAYER_MAX_HP-1U);
    g.lives=1U;g.player.grace=0U;
    g.enemy_bullets[0]=(GardenEnemyBullet){.x=g.player.x,.y=g.player.y,
        .age=GARDEN_BULLET_ARM_TICKS,.ttl=500U,.active=1U,.type=GARDEN_BULLET_DART,.colour=1U};
    garden_step(&g,0U);
    CHECK(g.mode==GARDEN_MODE_GAMEOVER && !g.player.active && !g.lives);
    g.demo_mode=1U;
    for(unsigned i=0;i<1000U;i++)garden_step(&g,0U);
    CHECK(g.mode==GARDEN_MODE_GAMEOVER); /* logic catchup must never replace wall time */
    g.paused=1U;g.demo_mode=0U;garden_step(&g,GARDEN_INPUT_RESTART);
    CHECK(g.mode==GARDEN_MODE_WAVES && !g.paused && g.player.active && g.tick==0U && !g.demo_mode && g.lives==GARDEN_PLAYER_MAX_HP);
    puts("PASS nonfatal HP loss continues, zero HP settles, no tick restart, paused restart restores10HP");
}

int main(void)
{
    idle_policy();gameover_policy();real_core_death();
    printf("PASS session/core: %u assertions\n",checks);return 0;
}
