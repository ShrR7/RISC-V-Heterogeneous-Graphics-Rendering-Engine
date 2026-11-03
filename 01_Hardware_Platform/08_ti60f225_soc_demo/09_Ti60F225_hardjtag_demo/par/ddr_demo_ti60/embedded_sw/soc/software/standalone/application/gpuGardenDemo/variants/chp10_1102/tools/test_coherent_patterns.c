/* SPDX-License-Identifier: GPL-3.0-or-later
 * Coherent appearance tests against production core and pinned Y mechanics.
 * No MMIO, JTAG, UART or host timing claimed as FPGA performance.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "../src/garden_core.c"
static unsigned checks,enemy_events,boss_trains,replay_steps,replay_runs;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
void baseline_init(uint32_t seed);
void baseline_step(uint16_t buttons);
size_t baseline_snapshot(void *out,size_t capacity);
size_t baseline_boss_size(void);
static unsigned check_group(const GardenGame *g,unsigned type,unsigned colour)
{
    unsigned n=0U;
    for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)if(g->enemy_bullets[i].active){
        const GardenEnemyBullet *b=&g->enemy_bullets[i];n++;
        CHECK(b->type==type && b->colour==colour);
        CHECK(b->age==0U && !garden_enemy_bullet_is_armed(b));
    }
    return n;
}
static void enemy_contract(void)
{
    unsigned banks[2][3]={{0U}},styles[2]={0U};
    for(unsigned type=1U;type<=2U;type++)for(unsigned seed=1U;seed<=1024U;seed++){
        GardenGame g;garden_init(&g,seed);
        GardenEnemy *e=&g.enemies[0];
        *e=(GardenEnemy){.active=1U,.type=(uint8_t)type,.x=320000,.y=120000,.aim_angle=256U};
        for(unsigned event=0U;event<6U;event++){
            memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));
            enemy_fire(&g,e);enemy_events++;
            unsigned colour=g.enemy_bullets[0].colour,bt=g.enemy_bullets[0].type;
            CHECK(colour>=1U && colour<=5U);
            unsigned count=check_group(&g,bt,colour),style;
            if(type==1U){
                CHECK(count==1U || count==3U);
                style=count==1U ? 0U:bt==GARDEN_BULLET_DART ? 1U:2U;
                CHECK(e->recycle>=36U && e->recycle<=60U);
            }else{
                CHECK(count==18U || count==12U || count==15U);
                style=count==18U ? 0U:count==12U ? 1U:2U;
                CHECK(e->recycle>=80U && e->recycle<=112U);
            }
            CHECK(!g.dropped_enemy_bullets);
            styles[type-1U]|=1U<<style;banks[type-1U][style]|=1U<<(colour-1U);
        }
    }
    for(unsigned t=0U;t<2U;t++){
        CHECK(styles[t]==7U);
        for(unsigned s=0U;s<3U;s++)CHECK(banks[t][s]==31U);
    }
    /* The enemy allocator's old partial-pool semantics are unchanged. */
    for(unsigned type=1U;type<=2U;type++)for(unsigned seed=1U;seed<=128U;seed++){
        GardenGame g;garden_init(&g,seed);
        GardenEnemy e={.active=1U,.type=(uint8_t)type,.x=320000,.y=120000,.aim_angle=256U};
        for(unsigned i=1U;i<GARDEN_MAX_ENEMY_BULLETS;i++)g.enemy_bullets[i].active=1U;
        enemy_fire(&g,&e);CHECK(g.enemy_bullets[0].active && g.enemy_bullets[0].colour>=1U && g.enemy_bullets[0].colour<=5U);
        CHECK(g.dropped_enemy_bullets==(type==1U ? 0U:11U) ||
              g.dropped_enemy_bullets==(type==1U ? 2U:14U) || g.dropped_enemy_bullets==(type==1U ? 2U:17U));
    }
    puts("PASS enemy:12288 firing events; one type+bank per event, all6 patterns each use all5 banks; one-free-slot allocator behavior retained");
}
static GardenGame boss_case(unsigned seed,unsigned attack,unsigned band)
{
    GardenGame g;garden_init(&g,seed);g.mode=GARDEN_MODE_BOSS;g.player.active=0U;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=band==0U?50000:band==1U?30000:15000,
        .max_hp=50000,.attack=(uint8_t)attack,.rhythm_state=GARDEN_BOSS_TRANSIT};return g;
}
static void boss_contract(void)
{
    unsigned banks[3]={0U},waits=0U;
    for(unsigned seed=1U;seed<=256U;seed++)for(unsigned a=1U;a<=3U;a++)for(unsigned band=0U;band<3U;band++){
        GardenGame g=boss_case(seed,a,band);
        for(unsigned i=0U;i<80U;i++)g.enemy_bullets[i].active=1U;
        boss_schedule_charge(&g,1U);boss_trains++;
        unsigned colour=g.boss.volley_colour,remaining=g.boss.burst_left;
        unsigned charge=g.boss.charge_ticks,rest=g.boss.rest_ticks,gap=g.boss.burst_gap;
        uint32_t rng=g.rng;
        CHECK(colour>=1U && colour<=5U && g.boss.rhythm_state==GARDEN_BOSS_WAIT);
        banks[a-1U]|=1U<<(colour-1U);
        for(unsigned n=0U;n<8U;n++){
            boss_schedule_charge(&g,0U);waits++;
            CHECK(g.boss.volley_colour==colour && g.rng==rng && g.boss.burst_left==remaining);
            CHECK(g.boss.charge_ticks==charge && g.boss.rest_ticks==rest && g.boss.burst_gap==gap);
            CHECK(g.laser.phase==GARDEN_LASER_OFF);
        }
        memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));boss_schedule_charge(&g,0U);
        CHECK(g.boss.volley_colour==colour && g.rng==rng && g.boss.rhythm_state==GARDEN_BOSS_CHARGE);
        CHECK((g.laser.phase==GARDEN_LASER_WARN)==(a==2U));
        unsigned pulses=0U;
        while(g.boss.burst_left){
            memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));
            /* A competing producer can force a WAIT between pulses. The
             * announced bank must survive even if health changes meanwhile. */
            if(pulses){
                unsigned blockers=81U-boss_pulse_size(a);
                for(unsigned i=0U;i<blockers;i++)g.enemy_bullets[i].active=1U;
                remaining=g.boss.burst_left;rng=g.rng;g.boss.hp=1;
                boss_fire_pulse(&g);CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT);
                CHECK(g.rng==rng && g.boss.volley_colour==colour && g.boss.burst_left==remaining);
                boss_schedule_charge(&g,0U);waits++;
                CHECK(g.boss.volley_colour==colour && g.rng==rng && g.boss.hp_band==band);
                memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));boss_schedule_charge(&g,0U);
                CHECK(g.boss.volley_colour==colour && g.rng==rng && g.boss.rhythm_state==GARDEN_BOSS_CHARGE);
            }
            boss_fire_pulse(&g);pulses++;
            CHECK(check_group(&g,a==3U?GARDEN_BULLET_DIAMOND:GARDEN_BULLET_DART,colour)==boss_pulse_size(a));
            CHECK(g.boss.volley_colour==colour && !g.dropped_enemy_bullets);
        }
        CHECK(pulses==(a==1U?1U:a==2U?6U:2U));
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_REST);
    }
    for(unsigned a=0U;a<3U;a++)CHECK(banks[a]==31U);
    printf("PASS boss:2304 complete trains, all3 HP bands, all5 banks per attack,64/54/72 total shots, initial+midtrain WAIT preserves bank; wait_checks=%u\n",waits);
}
static void compare_mechanics(GardenGame *game)
{
    unsigned char old[sizeof(GardenGame)];GardenGame normalized=*game;
    CHECK(baseline_snapshot(old,sizeof(old))==sizeof(GardenGame));
    for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)normalized.enemy_bullets[i].colour=0U;
    /* Only the newly occupied former padding byte and old padding are
     * excluded; all actual fields except projectile colour stay compared. */
    memset((unsigned char *)&normalized.boss+offsetof(GardenBoss,attack_bag)+1U,0,
        sizeof(GardenBoss)-offsetof(GardenBoss,attack_bag)-1U);
    if(memcmp(old,&normalized,sizeof(old))){
        for(unsigned i=0U;i<sizeof(old);i++)if(old[i]!=((unsigned char *)&normalized)[i]){
            fprintf(stderr,"Mechanical mismatch seed=%08X tick=%u offset=%u old=%u new=%u\n",
                game->initial_seed,game->tick,i,old[i],((unsigned char *)&normalized)[i]);break;
        }
        CHECK(0);
    }
    checks++;replay_steps++;
}
static void replay_contract(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    CHECK(sizeof(GardenGame)==9528U && sizeof(GardenBoss)==baseline_boss_size());
    for(unsigned s=0U;s<sizeof(seeds)/sizeof(seeds[0]);s++){
        GardenGame g;garden_init(&g,seeds[s]);baseline_init(seeds[s]);compare_mechanics(&g);
        unsigned terminal=0U;
        for(unsigned n=0U;n<16000U;n++){
            garden_step(&g,0U);baseline_step(0U);compare_mechanics(&g);
            if(g.mode==GARDEN_MODE_VICTORY || g.mode==GARDEN_MODE_GAMEOVER){terminal=1U;break;}
        }
        CHECK(terminal);
        printf("REPLAY seed=%08X terminal=%s tick=%u score=%u\n",seeds[s],
            g.mode==GARDEN_MODE_VICTORY?"VICTORY":"GAMEOVER",g.tick,g.score);
        /* Automatic restart, pause, manual control and explicit restart all
         * remain identical to Y as well as the normal automatic run. */
        for(unsigned n=0U;n<320U;n++){garden_step(&g,0U);baseline_step(0U);compare_mechanics(&g);}
        for(unsigned n=0U;n<40U;n++){
            uint16_t buttons=n==0U || n==10U?GARDEN_INPUT_PAUSE:n<20U?GARDEN_INPUT_LEFT:
                n<30U?GARDEN_INPUT_RIGHT:n==30U?GARDEN_INPUT_BOMB:0U;
            garden_step(&g,buttons);baseline_step(buttons);compare_mechanics(&g);
        }
        garden_step(&g,GARDEN_INPUT_RESTART);baseline_step(GARDEN_INPUT_RESTART);compare_mechanics(&g);replay_runs++;
    }
    printf("PASS mechanical replay:eight full Y-vs-current runs plus restart/pause/manual controls, all bytes identical except enemy-bullet colour and Boss former padding; steps=%u\n",replay_steps);
}
int main(void)
{
    enemy_contract();boss_contract();replay_contract();
    printf("RESULT checks=%u enemy_events=%u boss_trains=%u replay_runs=%u replay_steps=%u state_bytes=%u\n",
        checks,enemy_events,boss_trains,replay_runs,replay_steps,(unsigned)sizeof(GardenGame));return 0;
}
