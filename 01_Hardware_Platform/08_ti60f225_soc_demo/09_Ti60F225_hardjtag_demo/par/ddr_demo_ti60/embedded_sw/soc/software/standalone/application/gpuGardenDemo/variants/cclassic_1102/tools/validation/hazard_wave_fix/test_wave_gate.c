#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.c"
static unsigned checks;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,#c);exit(1);}}while(0)
static GardenGame fixture(void)
{
    GardenGame g;garden_init(&g,12345U);g.tick=999U;g.player.active=0U;g.demo_mode=0U;
    g.enemies[0]=(GardenEnemy){.active=1U,.type=GARDEN_ENEMY_STOPPER,.x=250000,.y=100000,.hp=95,.phase=2U,.target_time=1000,.recycle=1000U};
    g.enemy_count=0U;return g;
}
static void boundaries(void)
{
    GardenGame g=fixture(),before=g;
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.age=30U,.x=500000,.y=300000,.ttl=500U};before=g;
    start_boss(&g);CHECK(!memcmp(&before,&g,sizeof(g)));
    for(unsigned t=0U;t<300U;t++){
        garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_WAVES && !g.boss.active && g.enemies[0].active && g.enemies[0].hp==95);
        CHECK(wave_enemies_alive(&g)==1U && g.score==0U && !g.reward.items_spawned);
    }
    CHECK(g.tick==1299U && g.rng==before.rng);
    GardenPlayerBullet shot={.damage=95U,.x=250000,.y=100000};hit_target(&g,&shot,0U);
    CHECK(!g.enemies[0].active && g.combat.kills_total==1U && g.score==81U);
    CHECK(g.reward.items_spawned==1U && g.weapon_stats.heals_spawned==1U && g.pickups[0].active);
    garden_step(&g,0U);CHECK(g.tick==1300U && g.mode==GARDEN_MODE_BOSS && g.boss.active && !wave_enemies_alive(&g));
    CHECK(g.score==81U && g.reward.items_spawned==1U && g.pickups[0].active && g.weapon_stats.heals_spawned==1U);
    /* Deliberately preserve original natural retreat. It is not a kill. */
    g=fixture();g.tick=1100U;g.enemies[0].phase=5U;g.enemies[0].y=549999;g.enemies[0].vy=3000;
    garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_WAVES && !g.enemies[0].active && !g.score && !g.reward.items_spawned);
    garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_BOSS && !wave_enemies_alive(&g));
    g=fixture();garden_step(&g,GARDEN_INPUT_PAUSE);CHECK(g.paused && g.tick==999U && g.enemies[0].active);
    garden_step(&g,GARDEN_INPUT_RESTART);CHECK(g.mode==GARDEN_MODE_WAVES && !g.boss.active && g.tick==0U);
    puts("PASS gate:300tick persistent surviving target, stale count ignored, refused transition has no side effects; real kill score/heal preserved then Boss next tick; natural retreat retained; pause/restart");
}
static void runs(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    for(unsigned s=0U;s<8U;s++){
        GardenGame g,b;garden_init(&g,seeds[s]);unsigned boss_tick=0U,wait_ticks=0U,last_removal=0U,last_kill=0U,spawns=0U,wave_kills=0U,natural=0U,ended=0U;
        for(unsigned n=0U;n<18000U;n++){
            b=g;garden_step(&g,0U);
            CHECK(g.score==g.combat.hit_score_total+g.combat.kill_score_total+g.reward.graze_score_total+g.reward.item_score_total);
            CHECK(g.lives<=3U && g.reward.bombs+g.reward.bombs_used==2U && !g.reward.bullets_cleared && !g.combat.dropped_total);
            if(b.mode==GARDEN_MODE_WAVES && g.mode==GARDEN_MODE_WAVES){
                if(g.tick>=1000U)wait_ticks++;
                for(unsigned e=0U;e<GARDEN_MAX_ENEMIES;e++){
                    if(!b.enemies[e].active && g.enemies[e].active)spawns++;
                    if(b.enemies[e].active && !g.enemies[e].active){
                        unsigned killed=0U;for(unsigned k=0U;k<g.combat.count;k++)if(!g.combat.items[k].boss && g.combat.items[k].target_slot==e && g.combat.items[k].killed)killed=1U;
                        wave_kills+=killed;natural+=!killed;last_removal=g.tick;last_kill=killed;
                    }
                }
                CHECK(spawns==wave_kills+natural+wave_enemies_alive(&g));
            }
            if(b.mode==GARDEN_MODE_WAVES && g.mode==GARDEN_MODE_BOSS){
                boss_tick=g.tick;CHECK(g.tick>=1000U && !wave_enemies_alive(&b) && !wave_enemies_alive(&g));
                CHECK(boss_tick==(last_removal>=999U ? last_removal+1U:1000U));
            }
            if(g.mode>=GARDEN_MODE_VICTORY && !g.end_ticks){
                CHECK(boss_tick>0U);if(s==0U)CHECK(g.mode==GARDEN_MODE_VICTORY);
                printf("RUN seed=%u terminal=%u win_tick=%u score=%u boss_tick=%u waiting=%u last_wave_removal=%u last_removed_killed=%u spawns=%u wave_kills=%u natural_exits=%u bombs=%u impacts=%u damage=%u laser=%u hits=%u\n",seeds[s],g.mode,g.tick,g.score,boss_tick,wait_ticks,last_removal,last_kill,spawns,wave_kills,natural,g.weapon_stats.bombs_launched,g.weapon_stats.bomb_impacts,g.weapon_stats.bomb_damage,g.weapon_stats.laser_shots,g.reward.hits_taken);ended=1U;
            }
            if(g.tick<b.tick){CHECK(ended && g.reward.high_score>=b.score && g.reward.bombs==2U);printf("RESTART seed=%u best=%u\n",seeds[s],g.reward.high_score);break;}
        }
        CHECK(ended);
    }
}
int main(void){boundaries();runs();printf("RESULT status=PASS checks=%u runs=8 state_bytes=%u\n",checks,(unsigned)sizeof(GardenGame));return 0;}
