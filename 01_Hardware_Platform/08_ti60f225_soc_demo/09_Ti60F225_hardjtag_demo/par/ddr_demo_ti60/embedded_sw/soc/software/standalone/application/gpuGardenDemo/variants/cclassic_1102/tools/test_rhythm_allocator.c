/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent typed128-budget production reference. Complete old state is
 * compared byte for byte; only the appended temporary cursor is outside it.
 * Slot probes count actual allocator-loop iterations, not host wall time.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"

size_t rhythm_reference_size(void);
void rhythm_reference_init(uint32_t seed);
void rhythm_reference_load(const void *bytes,size_t length);
void rhythm_reference_copy(void *bytes,size_t length);
void rhythm_reference_step(uint16_t buttons);
unsigned rhythm_reference_countdown(void);

static uint64_t old_probes[2],new_probes[2];
static uint64_t compared_steps,boss_probe_steps,maximum_old,maximum_new;
static unsigned checks,seed_victories,seed_restarts;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)
void garden_host_alloc_probe(unsigned boss,unsigned slot)
{ CHECK(boss<2U && slot<192U,"new allocator trace stays inside pool");new_probes[boss]++; }
void garden_reference_alloc_probe(unsigned boss,unsigned slot)
{ CHECK(boss<2U && slot<192U,"reference allocator trace stays inside pool");old_probes[boss]++; }

static void compare(const GardenGame *g)
{
    unsigned char reference[sizeof(GardenGame)];
    rhythm_reference_copy(reference,rhythm_reference_size());
    CHECK(!memcmp(reference,g,rhythm_reference_size()),
          "complete original128-budget state byte identical: slots, RNG, HP, score, rhythm, effects, counters");
    CHECK(!g->boss_alloc_cursor && !g->boss_alloc_enabled,"temporary pulse cursor zero at every public step boundary");
    CHECK(garden_boss_fire_countdown(g)==rhythm_reference_countdown(),"true charge forecast unchanged");
}

static void step(GardenGame *g,uint16_t input)
{
    uint64_t old_wave=old_probes[0],new_wave=new_probes[0],old_boss=old_probes[1],new_boss=new_probes[1];
    garden_step(g,input); rhythm_reference_step(input);compare(g);compared_steps++;
    CHECK(old_probes[0]-old_wave==new_probes[0]-new_wave,"wave minimum-slot independent scan performs identical work");
    uint64_t before=old_probes[1]-old_boss,after=new_probes[1]-new_boss;
    CHECK(after<=before,"monotone Boss allocation cannot increase slot examinations");
    if(before) {
        boss_probe_steps++;
        if(before>maximum_old) maximum_old=before;
        if(after>maximum_new) maximum_new=after;
    }
}

static void load(GardenGame *g)
{ rhythm_reference_load(g,rhythm_reference_size());compare(g); }

static void pulse_fixtures(void)
{
    for(unsigned attack=1U;attack<=3U;attack++) for(unsigned placement=0U;placement<4U;placement++)
        for(unsigned seed=1U;seed<=32U;seed++) {
            GardenGame g;garden_init(&g,seed);g.tick=100U;g.mode=GARDEN_MODE_BOSS;
            g.demo_mode=0U;g.player.active=0U;
            g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=50000,.max_hp=50000,
                .attack=(uint8_t)attack,.status=(uint8_t)(seed%4U),
                .rhythm_state=GARDEN_BOSS_CHARGE,.rhythm_ticks=1U,.charge_ticks=20U,
                .rest_ticks=80U,.burst_gap=12U,.burst_left=1U};
            unsigned pulse=attack==1U ? 64U:attack==2U ? 9U:36U;
            unsigned occupied=placement ? 128U-pulse : 0U;
            for(unsigned i=0U;i<occupied;i++) {
                unsigned slot=placement==1U ? i : placement==2U ? 191U-i : (i*5U+seed)%192U;
                g.enemy_bullets[slot]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DART,
                    .x=320000,.y=200000,.ttl=500U,.cord_wait=300};
            }
            /* Fully fragmented, stale count and all status/RNG variants. */
            g.enemy_bullet_count=191U;load(&g);
            uint64_t before=old_probes[1],after=new_probes[1];step(&g,0U);
            CHECK(g.rhythm.shots_admitted==1U && !g.dropped_enemy_bullets,"one unchanged complete64/9/36generator pulse admitted");
            CHECK(new_probes[1]-after<=192U,"single pulse examines each physical slot at most once");
            if(seed==1U && placement<2U)
                printf("PULSE_WORK: attack=%u occupied=%u emitted=%u old_probes=%llu new_probes=%llu\n",
                    attack,occupied,pulse,(unsigned long long)(old_probes[1]-before),(unsigned long long)(new_probes[1]-after));
        }
    puts("PASS:384full-pulse fixtures, fragmented/front/back slots, all status/RNG variants, stale counts and exact state equivalence");
}

static void complete_trajectories(void)
{
    for(unsigned trial=0U;trial<18U;trial++) {
        unsigned seed=trial ? trial : 12345U;GardenGame g;
        garden_init(&g,seed);rhythm_reference_init(seed);compare(&g);
        for(unsigned i=0U;i<(trial ? 12000U : 24000U);i++) {
            uint16_t input=0U;
            if(trial==17U) {
                unsigned phase=i%1200U;
                if(phase<150U) input=GARDEN_INPUT_LEFT;
                else if(phase<300U) input=GARDEN_INPUT_RIGHT|GARDEN_INPUT_UP|GARDEN_INPUT_SLOW;
                else if(phase<450U) input=GARDEN_INPUT_DOWN;
                else if(phase==600U) input=GARDEN_INPUT_TOGGLE_DEMO;
                else if(phase==800U || phase==811U) input=GARDEN_INPUT_PAUSE;
                else if(phase==1000U) input=GARDEN_INPUT_RESTART;
            }
            unsigned old_mode=g.mode,old_tick=g.tick;step(&g,input);
            CHECK(g.enemy_bullet_count<=128U && !g.dropped_enemy_bullets,"natural trajectories preserve live128 budget and zero dropped projectiles");
            if(!trial) {
                if(g.mode!=old_mode && g.mode==GARDEN_MODE_VICTORY) {
                    seed_victories++;
                    CHECK(g.tick==3979U && g.score==1959U && g.lives==2U && g.rhythm.capacity_wait_ticks==803U,
                          "accepted128-budget seed timeline and actual score/damage/lives remain unchanged");
                }
                if(g.tick<old_tick) seed_restarts++;
            }
        }
    }
    CHECK(seed_victories>=5U && seed_restarts>=5U,"independent equivalence covers five actual vulnerable seed12345victories/restarts");
    printf("PASS: whole trajectories seeds=18 compared_steps=%llu seed12345_victories=%u seed12345_restarts=%u\n",
        (unsigned long long)compared_steps,seed_victories,seed_restarts);
}

int main(void)
{
    CHECK(rhythm_reference_size()==offsetof(GardenGame,boss_alloc_cursor),"reference exact original state size equals new temporary-field offset");
    CHECK(sizeof(GardenGame)==rhythm_reference_size()+4U,"allocation adds only4temporary bytes, no frame/heap/scratch buffer");
    pulse_fixtures();complete_trajectories();
    CHECK(old_probes[0]==new_probes[0] && old_probes[1]>new_probes[1] && boss_probe_steps,
          "wave work identical, Boss allocator loop work actually reduced");
    printf("PASS: allocator equivalence checks=%u compared_steps=%llu reference_bytes=%u new_bytes=%u wave_old=%llu wave_new=%llu boss_old=%llu boss_new=%llu boss_steps=%llu max_boss_old=%llu max_boss_new=%llu\n",
        checks,(unsigned long long)compared_steps,(unsigned)rhythm_reference_size(),(unsigned)sizeof(GardenGame),
        (unsigned long long)old_probes[0],(unsigned long long)new_probes[0],
        (unsigned long long)old_probes[1],(unsigned long long)new_probes[1],
        (unsigned long long)boss_probe_steps,(unsigned long long)maximum_old,(unsigned long long)maximum_new);
    return 0;
}
