#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.c"
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static GardenGame fixture(void)
{GardenGame g;garden_init(&g,12345U);g.demo_mode=0U;g.player.active=0U;return g;}
static void target(GardenGame *g,unsigned slot,unsigned type,unsigned hp)
{g->enemies[slot]=(GardenEnemy){.active=1U,.type=(uint8_t)type,.hp=(int32_t)hp,.x=250000,.y=100000,.phase=2U,.target_time=1000,.recycle=1000U};}
static void kill(GardenGame *g,unsigned slot)
{GardenPlayerBullet shot={.damage=(uint16_t)g->enemies[slot].hp};g->combat.count=0U;hit_target(g,&shot,slot);}
static void queue_contract(void)
{
    GardenGame g=fixture();g.tick=1000U;run_waves(&g);
    CHECK(g.wave_next==8U && wave_enemies_alive(&g)==8U && !g.boss.active);
    uint32_t rng=g.rng;
    for(unsigned i=0;i<20U;i++){run_waves(&g);CHECK(g.wave_next==8U && g.rng==rng && wave_enemies_alive(&g)==8U);}
    kill(&g,0U);CHECK(g.wave_kills==1U);
    GardenGame expected=g;int32_t y=-30000-grand(&expected,140000);
    run_waves(&g);CHECK(g.wave_next==9U && g.rng==expected.rng && wave_enemies_alive(&g)==8U);
    CHECK(g.enemies[0].type==GARDEN_ENEMY_DARTER && g.enemies[0].x==460000 && g.enemies[0].y==y);
    rng=g.rng;run_waves(&g);CHECK(g.wave_next==9U && g.rng==rng);
    kill(&g,1U);expected=g;int32_t x=220000+grand(&expected,200000);run_waves(&g);
    CHECK(g.wave_next==10U && g.wave_kills==2U && g.rng==expected.rng && g.enemies[1].type==GARDEN_ENEMY_STOPPER && g.enemies[1].x==x);
    rng=g.rng;run_waves(&g);CHECK(g.wave_next==10U && g.rng==rng && g.mode==GARDEN_MODE_WAVES);
    for(unsigned i=0U;i<8U;i++)kill(&g,i);
    CHECK(g.wave_kills==10U && !wave_enemies_alive(&g));run_waves(&g);CHECK(g.mode==GARDEN_MODE_BOSS);
    CHECK(g.reward.items_spawned+g.reward.items_dropped==10U);
    puts("PASS queue:8full slots defer exact task/no RNG, ordered pendingDART then STOPPER delivered once;10real kills; no late duplicate spawn");
}
static void retention_contract(void)
{
    for(unsigned type=GARDEN_ENEMY_DARTER;type<=GARDEN_ENEMY_STOPPER;type++){
        GardenGame g=fixture();target(&g,0U,type,95U);g.enemies[0].phase=3U;g.enemies[0].moves=3U;g.enemies[0].target_time=1;g.enemies[0].recycle=1000U;
        for(unsigned i=0U;i<500U;i++){run_enemies(&g);CHECK(g.enemies[0].active && g.enemies[0].hp==95 && g.enemies[0].phase!=5U);}
        CHECK(g.enemies[0].x==250000 && g.enemies[0].y==100000 && !g.score && !g.wave_kills && !g.reward.items_spawned);
        for(unsigned reason=0U;reason<4U;reason++){
            g=fixture();target(&g,0U,type,225U);g.enemies[0].recycle=1000U;
            if(reason==0U){g.enemies[0].phase=5U;g.enemies[0].y=549999;g.enemies[0].vy=3000;}
            if(reason==1U){g.enemies[0].phase=4U;g.enemies[0].x=700000;g.enemies[0].vx=5000;g.enemies[0].target_time=990;}
            if(reason==2U)g.enemies[0].age=1600U;
            if(reason==3U)g.enemies[0].age=65535U;
            uint32_t rng=g.rng;run_enemies(&g);
            CHECK(g.enemies[0].active && g.enemies[0].hp==225 && g.enemies[0].y<0 && g.enemies[0].phase==0U && g.enemies[0].age<=1U);
            CHECK(g.enemies[0].x>=80000 && g.enemies[0].x<=560000 && g.rng==rng);
            CHECK(!g.score && !g.wave_kills && !g.reward.items_spawned);
        }
    }
    puts("PASS retention:STOPPER/exhaustedDART stay for500ticks; phase5/off-field/1600/65535age return above field with HP/no RNG/no reward/no immunity");
}
static void gate_contract(void)
{
    GardenGame g=fixture();g.tick=999U;g.wave_next=10U;g.wave_kills=9U;target(&g,0U,GARDEN_ENEMY_STOPPER,95U);g.enemy_count=0U;
    GardenGame before=g;start_boss(&g);CHECK(!memcmp(&g,&before,sizeof(g)));
    for(unsigned i=0U;i<300U;i++){garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_WAVES && g.enemies[0].active && g.enemies[0].hp==95 && !g.boss.active);}
    CHECK(g.tick==1299U && g.wave_next==10U && g.wave_kills==9U);
    kill(&g,0U);CHECK(g.wave_kills==10U && g.score==81U && g.weapon_stats.heals_spawned==1U && g.pickups[0].active);
    garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_BOSS && !wave_enemies_alive(&g) && g.boss.active && g.score==81U && g.pickups[0].active);
    g=fixture();g.tick=1000U;g.wave_next=10U;g.wave_kills=9U;run_waves(&g);CHECK(g.mode==GARDEN_MODE_WAVES && !g.boss.active);
    g=fixture();g.tick=39U;garden_step(&g,GARDEN_INPUT_PAUSE);CHECK(g.tick==39U && !g.wave_next && !g.wave_kills);
    garden_step(&g,GARDEN_INPUT_RESTART);CHECK(!g.wave_next && !g.wave_kills && !g.tick && g.reward.bombs==2U);
    puts("PASS gate:1000tick survivor persists300ticks/stale0count; refused call no side effects; final real kill preserves81score/heal;10kills required even if zeroalive; pause/restart");
}
static void runs(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    for(unsigned s=0U;s<8U;s++){
        GardenGame g,b;garden_init(&g,seeds[s]);unsigned boss_tick=0U,last_kill=0U,oracle_kills=0U,wave_peak=0U,active_peak=0U,boss_peak=0U,ended=0U;
        for(unsigned n=0U;n<24000U;n++){
            b=g;garden_step(&g,0U);
            CHECK(g.score==g.combat.hit_score_total+g.combat.kill_score_total+g.reward.graze_score_total+g.reward.item_score_total);
            CHECK(g.reward.bombs+g.reward.bombs_used==2U && !g.reward.bullets_cleared && !g.combat.dropped_total);
            if(g.tick<b.tick){CHECK(ended && g.reward.high_score>=b.score && !g.wave_next && !g.wave_kills);printf("RESTART seed=%u best=%u\n",seeds[s],g.reward.high_score);break;}
            if(b.mode==GARDEN_MODE_WAVES){
                unsigned kills=0U;for(unsigned k=0U;k<g.combat.count;k++)if(g.combat.items[k].killed && !g.combat.items[k].boss)kills++;
                oracle_kills+=kills;if(kills)last_kill=g.tick;
                CHECK(g.wave_kills==oracle_kills && g.wave_next<=10U);
                CHECK(g.wave_next==oracle_kills+wave_enemies_alive(&g));
            }
            if(g.mode==GARDEN_MODE_WAVES){if(g.enemy_bullet_count>wave_peak)wave_peak=g.enemy_bullet_count;unsigned alive=wave_enemies_alive(&g);if(alive>active_peak)active_peak=alive;}
            if(g.mode==GARDEN_MODE_BOSS){CHECK(g.enemy_bullet_count<=80U);if(g.enemy_bullet_count>boss_peak)boss_peak=g.enemy_bullet_count;}
            if(b.mode==GARDEN_MODE_WAVES && g.mode==GARDEN_MODE_BOSS){boss_tick=g.tick;CHECK(g.wave_next==10U && g.wave_kills==10U && oracle_kills==10U && !wave_enemies_alive(&g));CHECK(boss_tick==last_kill+1U || (boss_tick==1000U && last_kill<1000U));}
            if(g.mode>=GARDEN_MODE_VICTORY && !g.end_ticks){
                if(!s)CHECK(g.mode==GARDEN_MODE_VICTORY);
                ended=1U;
                printf("RUN seed=%u terminal=%u win_tick=%u score=%u boss_tick=%u last_wave_kill=%u spawned=%u killed=%u oracle_kills=%u waves_eb_peak=%u waves_active_peak=%u boss_eb_peak=%u dropped_eb=%u combat_drop=%u bomb_damage=%u laser=%u hits=%u\n",seeds[s],g.mode,g.tick,g.score,boss_tick,last_kill,g.wave_next,g.wave_kills,oracle_kills,wave_peak,active_peak,boss_peak,g.dropped_enemy_bullets,g.combat.dropped_total,g.weapon_stats.bomb_damage,g.weapon_stats.laser_shots,g.reward.hits_taken);
            }
        }
        CHECK(ended);
    }
}
int main(void){queue_contract();retention_contract();gate_contract();runs();printf("RESULT status=PASS checks=%u runs=8 state_bytes=%u\n",checks,(unsigned)sizeof(GardenGame));return 0;}
