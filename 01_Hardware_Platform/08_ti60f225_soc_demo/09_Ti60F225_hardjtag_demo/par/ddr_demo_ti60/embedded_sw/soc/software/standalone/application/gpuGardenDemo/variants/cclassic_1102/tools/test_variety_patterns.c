/* SPDX-License-Identifier: GPL-3.0-or-later
 * Variety schedule contracts, independent of old fixed-score/run fixtures.
 * Uses the production core directly; no MMIO, UART, JTAG or FPS assertions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
static unsigned checks,full_runs,full_wins;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t independent_successor(uint32_t x)
{
    uint64_t wide=x;
    wide^=(wide<<13)&UINT32_MAX;wide&=UINT32_MAX;
    wide^=wide>>17;
    wide^=(wide<<5)&UINT32_MAX;
    return (uint32_t)wide;
}
static uint32_t fingerprint(uint32_t hash,uint32_t value)
{return (hash^value)*UINT32_C(16777619);}
static GardenGame boss_case(uint32_t seed,unsigned attack,unsigned band)
{
    GardenGame g;garden_init(&g,seed);g.mode=GARDEN_MODE_BOSS;g.player.active=0U;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=band==0U ? 50000:band==1U ? 30000:15000,
        .max_hp=50000,.attack=(uint8_t)attack,.rhythm_state=GARDEN_BOSS_TRANSIT};
    return g;
}
static void restart_contract(void)
{
    GardenGame g;garden_init(&g,0U);CHECK(g.initial_seed==UINT32_C(0x47617264));
    uint32_t expected=g.initial_seed;
    for(unsigned n=0U;n<8192U;n++){
        uint32_t previous=expected;expected=independent_successor(expected);
        g.score=500U+n;g.reward.high_score=100000U;g.rng=n+1U;
        garden_step(&g,GARDEN_INPUT_RESTART);
        CHECK(g.initial_seed==expected && expected!=0U && expected!=previous);
        CHECK(g.rng==expected && g.tick==0U && g.mode==GARDEN_MODE_WAVES);
        CHECK(g.reward.high_score==100000U && g.reward.bombs==2U && g.lives==3U);
    }
    puts("PASS restart:8192 distinct consecutive successor steps, no dependence on run length/RNG consumption, best retained");
}
static void shuffle_contract(void)
{
    unsigned first_mask=0U;uint32_t hashes[64];
    for(unsigned seed=1U;seed<=64U;seed++){
        GardenGame g=boss_case(seed,0U,0U);uint32_t hash=2166136261U;unsigned prev=0U;
        for(unsigned bag=0U;bag<100U;bag++){
            unsigned seen=0U;
            for(unsigned pick=0U;pick<3U;pick++){
                boss_choose_attack(&g);unsigned a=g.boss.attack;
                CHECK(a>=1U && a<=3U && a!=prev && !(seen&(1U<<(a-1U))));
                seen|=1U<<(a-1U);prev=a;hash=fingerprint(hash,a);
                CHECK((g.boss.attack_bag&seen)==0U);
                if(!bag && !pick)first_mask|=1U<<(a-1U);
            }
            CHECK(seen==7U && g.boss.attack_bag==0U);
        }
        hashes[seed-1U]=hash;
    }
    unsigned distinct=0U;
    for(unsigned i=0U;i<64U;i++){unsigned is_new=1U;for(unsigned j=0U;j<i;j++)is_new&=hashes[i]!=hashes[j];distinct+=is_new;}
    CHECK(first_mask==7U && distinct>=60U);
    printf("PASS shuffle:64 seeds x100 complete bags, all3 per bag, no adjacent repeat, distinct_sequences=%u\n",distinct);
}
static void timing_and_capacity_contract(void)
{
    unsigned charges[3]={0U},rests[3]={0U},gaps[3]={0U};
    for(unsigned band=0U;band<3U;band++)for(unsigned attack=1U;attack<=3U;attack++)for(unsigned seed=1U;seed<=256U;seed++){
        GardenGame g=boss_case(seed,attack,band);boss_schedule_charge(&g,1U);
        unsigned c0=20U-4U*band,r0=80U-16U*band,g0=attack==2U ? 6U-band:12U-2U*band;
        CHECK(g.boss.charge_ticks>=c0 && g.boss.charge_ticks<=c0+8U);
        CHECK(g.boss.rest_ticks>=r0 && g.boss.rest_ticks<=r0+24U);
        CHECK(g.boss.burst_gap>=g0 && g.boss.burst_gap<=g0+(attack==2U ? 2U:4U));
        CHECK(g.boss.hp_band==band && g.boss.rhythm_state==GARDEN_BOSS_CHARGE);
        CHECK(g.boss.burst_left==(attack==1U ? 1U:attack==2U ? 6U:2U));
        CHECK(boss_pulse_size(attack)*g.boss.burst_left<=80U);
        charges[band]|=1U<<(g.boss.charge_ticks-c0);
        if(g.boss.rest_ticks-r0<32U)rests[band]|=1U<<(g.boss.rest_ticks-r0);
        gaps[band]|=1U<<(g.boss.burst_gap-g0);
        uint32_t rng=g.rng;unsigned chosen=g.boss.attack;
        unsigned charge=g.boss.charge_ticks,rest=g.boss.rest_ticks,gap=g.boss.burst_gap;
        unsigned need=boss_pulse_size(attack)*g.boss.burst_left;
        /* Leave one slot fewer than a complete train; admission is all-or-none. */
        for(unsigned i=0U;i<81U-need;i++)g.enemy_bullets[i]=(GardenEnemyBullet){.active=1U,.ttl=500U};
        g.laser.phase=GARDEN_LASER_OFF;
        boss_schedule_charge(&g,0U);
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT && g.laser.phase==GARDEN_LASER_OFF);
        for(unsigned n=0U;n<8U;n++){
            boss_schedule_charge(&g,0U);
            CHECK(g.rng==rng && g.boss.attack==chosen && g.boss.charge_ticks==charge &&
                  g.boss.rest_ticks==rest && g.boss.burst_gap==gap);
        }
        memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));
        boss_schedule_charge(&g,0U);
        CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==charge && g.rng==rng);
        CHECK((g.laser.phase==GARDEN_LASER_WARN)==(attack==2U));
        if(attack==2U)CHECK(g.laser.ticks==40U && g.laser.x[0]==278000 && g.laser.x[1]==363000 && g.laser.y==139000);
        unsigned initial_train=g.boss.burst_left,emitted=0U;
        while(g.boss.burst_left){
            unsigned before=0U;for(unsigned i=0U;i<192U;i++)before+=g.enemy_bullets[i].active!=0U;
            boss_fire_pulse(&g);emitted++;
            unsigned after=0U;for(unsigned i=0U;i<192U;i++)after+=g.enemy_bullets[i].active!=0U;
            CHECK(after-before==boss_pulse_size(attack));
            CHECK(after<=80U && !g.dropped_enemy_bullets && g.boss.attack==chosen);
        }
        CHECK(emitted==initial_train && g.boss.rhythm_state==GARDEN_BOSS_REST);
    }
    for(unsigned band=0U;band<3U;band++)CHECK(charges[band]==511U && rests[band]==0x1FFFFFFU && gaps[band]==31U);
    puts("PASS timing:2304 train schedules cover all timing values/HP bands; WAIT keeps RNG/type/times, complete64/54/72 trains, original laser mounts+40tick warning");
}
static void enemy_styles_contract(void)
{
    unsigned small_mask=0U,large_mask=0U,colour_mask=0U,first_small=0U,first_large=0U;
    for(unsigned type=1U;type<=2U;type++)for(unsigned seed=1U;seed<=512U;seed++){
        GardenGame g;garden_init(&g,seed);
        GardenEnemy *e=&g.enemies[0];*e=(GardenEnemy){.active=1U,.type=(uint8_t)type,.hp=3800,.x=320000,.y=120000,.aim_angle=256U};
        enemy_fire(&g,e);unsigned count=0U,btype=0U;
        for(unsigned i=0U;i<192U;i++)if(g.enemy_bullets[i].active){
            GardenEnemyBullet *b=&g.enemy_bullets[i];count++;btype=b->type;
            CHECK(!garden_enemy_bullet_is_armed(b) && b->age==0U && b->cord_wait==6 && b->colour>=1U && b->colour<=5U);
            colour_mask|=1U<<(b->colour-1U);
        }
        if(type==1U){
            CHECK(count==1U || count==3U);CHECK(e->recycle>=36U && e->recycle<=60U);
            small_mask|=count==1U ? 1U:btype==GARDEN_BULLET_DART ? 2U:4U;
        }else{
            CHECK(count==12U || count==15U || count==18U);CHECK(e->recycle>=80U && e->recycle<=112U);
            large_mask|=count==18U ? 1U:count==12U ? 2U:4U;
        }
        CHECK(!g.dropped_enemy_bullets);
        /* Entering the firing hover samples a first-delay, rather than
         * consuming it during the long entrance when shots are disabled. */
        memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));
        e->phase=1U;e->target_time=1;e->recycle=0U;
        run_enemies(&g);
        CHECK(e->phase==2U && e->recycle>0U);
        if(type==1U){CHECK(e->recycle>=8U && e->recycle<=24U);first_small|=1U<<(e->recycle-8U);}
        else{CHECK(e->recycle>=12U && e->recycle<=32U);first_large|=1U<<(e->recycle-12U);}
        for(unsigned i=0U;i<192U;i++)CHECK(!g.enemy_bullets[i].active);
    }
    CHECK(small_mask==7U && large_mask==7U && colour_mask==31U);
    CHECK(first_small==0x1FFFFU && first_large==0x1FFFFFU);
    puts("PASS enemy:all6 styles and all5 appearance banks; bounded8..24/12..32 first delay,36..60/80..112 repeats, age0 formation");
}
static void strict_wave_contract(void)
{
    GardenGame g;garden_init(&g,12345U);g.tick=2000U;g.wave_next=8U;g.wave_kills=7U;
    for(unsigned i=0U;i<8U;i++)g.enemies[i]=(GardenEnemy){.active=1U,.type=1U,.hp=300,.x=100000,.y=100000};
    uint32_t rng=g.rng;run_waves(&g);CHECK(g.wave_next==8U && g.rng==rng && g.mode==GARDEN_MODE_WAVES);
    memset(g.enemies,0,sizeof(g.enemies));run_waves(&g);
    CHECK(g.wave_next==10U && wave_enemies_alive(&g)==2U && g.mode==GARDEN_MODE_WAVES);
    g.wave_kills=9U;memset(g.enemies,0,sizeof(g.enemies));
    g.enemies[0]=(GardenEnemy){.active=1U,.type=1U,.hp=300,.x=100000,.y=100000};
    g.enemy_count=0U;run_waves(&g);CHECK(g.mode==GARDEN_MODE_WAVES && g.enemies[0].hp==300);
    GardenPlayerBullet shot={.x=100000,.y=100000,.damage=300U};hit_target(&g,&shot,0U);
    CHECK(g.wave_kills==10U && !wave_enemies_alive(&g));run_waves(&g);
    CHECK(g.mode==GARDEN_MODE_BOSS && g.boss.attack>=1U && g.boss.attack<=3U);
    puts("PASS waves:full-pool deferral without RNG draws, ten real spawns+kills and actual empty slots required for Boss");
}
static void full_replay_contract(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U,
        4U,5U,6U,7U,0xC0FFEEU,0x10203040U,0x80000001U,0xFFFFFFFFU};
    uint32_t first_hash=0U;unsigned changed=0U;
    for(unsigned s=0U;s<sizeof(seeds)/sizeof(seeds[0]);s++){
        GardenGame a,b;garden_init(&a,seeds[s]);garden_init(&b,seeds[s]);
        unsigned terminal=0U,boss_seen=0U,first_train=1U;uint32_t trace=2166136261U,previous_charge=0U;
        for(unsigned n=0U;n<16000U;n++){
            GardenGame before=a;
            garden_step(&a,0U);garden_step(&b,0U);CHECK(memcmp(&a,&b,sizeof(a))==0);
            CHECK(a.combat.count<=33U && !a.combat.dropped_total && !a.dropped_enemy_bullets);
            CHECK(a.lives<=3U && a.reward.bombs+a.reward.bombs_used==2U && !a.reward.bullets_cleared);
            CHECK(a.score==a.combat.hit_score_total+a.combat.kill_score_total+a.reward.graze_score_total+a.reward.item_score_total);
            CHECK(a.hazard.armed+a.hazard.forming==a.enemy_bullet_count);
            if(a.mode==GARDEN_MODE_BOSS){
                boss_seen=1U;CHECK(a.wave_next==10U && a.wave_kills==10U && !a.enemy_count);
                CHECK(a.enemy_bullet_count<=80U);
                if(a.rhythm.charge_windows!=previous_charge){
                    trace=fingerprint(trace,a.tick);trace=fingerprint(trace,a.boss.attack);
                    trace=fingerprint(trace,a.boss.charge_ticks);trace=fingerprint(trace,a.boss.rest_ticks);
                    previous_charge=a.rhythm.charge_windows;first_train=0U;
                }
            }
            if(!first_train && a.boss.attack==before.boss.attack && before.boss.rhythm_state==GARDEN_BOSS_CHARGE &&
                a.boss.rhythm_state==GARDEN_BOSS_CHARGE)CHECK(a.boss.charge_ticks==before.boss.charge_ticks);
            if(a.mode==GARDEN_MODE_VICTORY || a.mode==GARDEN_MODE_GAMEOVER){
                terminal=1U;full_wins+=a.mode==GARDEN_MODE_VICTORY;
                printf("RUN seed=%u terminal=%s tick=%u score=%u peak=%u attack_counts=%u/%u/%u laser=%u/%u/%u bombs=%u/%u heal=%u/%u/%u hits=%u trace=%08X\n",
                    seeds[s],a.mode==GARDEN_MODE_VICTORY?"VICTORY":"GAMEOVER",a.tick,a.score,a.enemy_bullet_peak,
                    a.boss_attack_shots[0],a.boss_attack_shots[1],a.boss_attack_shots[2],
                    a.weapon_stats.laser_warnings,a.weapon_stats.laser_shots,a.weapon_stats.laser_hits,
                    a.weapon_stats.bombs_launched,a.weapon_stats.bomb_damage,a.weapon_stats.heals_spawned,
                    a.weapon_stats.heals_collected,a.weapon_stats.heals_restored,a.reward.hits_taken,trace);
                CHECK(boss_seen && a.boss_attack_shots[0] && a.boss_attack_shots[1] && a.boss_attack_shots[2]);
                uint32_t seed=a.initial_seed,best=a.reward.high_score;
                /* Automatic end-state restart also changes seed, and its
                 * complete state must replay identically, including pickups. */
                for(unsigned end=0U;end<320U;end++){garden_step(&a,0U);garden_step(&b,0U);CHECK(!memcmp(&a,&b,sizeof(a)));}
                CHECK(a.tick==0U && a.initial_seed==independent_successor(seed) && a.initial_seed!=seed);
                CHECK(a.reward.high_score>=best && a.reward.bombs==2U && a.lives==3U);
                for(unsigned p=0U;p<24U;p++){garden_step(&a,GARDEN_INPUT_PAUSE*(p==0U));garden_step(&b,GARDEN_INPUT_PAUSE*(p==0U));CHECK(!memcmp(&a,&b,sizeof(a)) && a.paused);}
                CHECK(a.tick==0U && a.initial_seed==independent_successor(seed));
                garden_step(&a,GARDEN_INPUT_PAUSE);garden_step(&b,GARDEN_INPUT_PAUSE);CHECK(!memcmp(&a,&b,sizeof(a)) && !a.paused && a.tick==1U);
                break;
            }
        }
        CHECK(terminal);full_runs++;
        if(!s)first_hash=trace;else changed+=trace!=first_hash;
    }
    CHECK(full_runs==16U && full_wins>=8U && changed>=14U);
    printf("PASS replay:16 seeds, byte-identical every step+end+pause/restart, varied_timing_traces=%u/15\n",changed);
}
int main(void)
{
    restart_contract();shuffle_contract();timing_and_capacity_contract();enemy_styles_contract();strict_wave_contract();full_replay_contract();
    printf("RESULT checks=%u runs=%u wins=%u state_bytes=%u\n",checks,full_runs,full_wins,(unsigned)sizeof(GardenGame));
    return 0;
}
