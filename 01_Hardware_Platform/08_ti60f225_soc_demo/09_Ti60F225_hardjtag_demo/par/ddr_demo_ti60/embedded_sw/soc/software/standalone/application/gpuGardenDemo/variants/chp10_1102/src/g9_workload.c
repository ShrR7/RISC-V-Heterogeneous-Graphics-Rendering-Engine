/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "g9_workload.h"
#include <string.h>

#define G9_WORKLOAD_SEED UINT32_C(0x4739574C)
static G9WorkloadStats stats;
static uint32_t bound_seed,last_tick;

static unsigned active_slots(const GardenGame *g)
{
    unsigned n=0U;
    for(unsigned i=0U;i<garden_enemy_scan_limit;i++)n+=g->enemy_bullets[i].active!=0U;
    return n;
}
static void observe(const GardenGame *g)
{
    unsigned n=active_slots(g);stats.last_live=n;
    if(!stats.observations || n<stats.min_live)stats.min_live=n;
    if(!stats.observations || n>stats.max_live)stats.max_live=n;
    stats.observations++;
    stats.above_target+=n>stats.target;stats.below_target+=n<stats.target;
}
static void populate(GardenEnemyBullet *b,unsigned i)
{
    static const int16_t vx[8]={350,250,0,-250,-350,-250,0,250};
    static const int16_t vy[8]={0,250,350,250,0,-250,-350,-250};
    unsigned direction=(i+i/17U)&7U;
    memset(b,0,sizeof(*b));
    /* Deterministic distributed positions,with half-pixel inset so outward
     * initial movement never repeatedly wraps a freshly populated boundary. */
    b->x=20500+(int32_t)((i*373U)%600U)*1000;
    b->y=20500+(int32_t)((i*193U)%320U)*1000;
    b->vx=vx[direction];b->vy=vy[direction];
    b->angle=b->cord_angle=(uint16_t)(direction*128U);
    b->age=GARDEN_BULLET_ARM_TICKS;b->ttl=60000U;b->cord_wait=-1;
    b->active=1U;b->type=GARDEN_BULLET_DIAMOND;b->colour=2U;
}
static unsigned epoch_changed(const GardenGame *g)
{
    if(g->initial_seed!=bound_seed || g->tick<last_tick) {
        stats.restart_cancelled++;stats.target=0U;return 1U;
    }
    last_tick=g->tick;return 0U;
}
int g9_workload_set(GardenGame *g,unsigned count)
{
    if(!g || (count!=0U && count!=128U && count!=512U && count!=1024U &&
              count!=1500U && count!=2000U))return -1;
    uint32_t best=g->reward.high_score>g->score ? g->reward.high_score:g->score;
    uint32_t generation=stats.generation+1U;
    uint32_t seed=count ? G9_WORKLOAD_SEED:g->initial_seed;
    garden_init(g,seed);g->reward.high_score=best;
    if(count)garden_enemy_scan_limit=count;
    memset(&stats,0,sizeof(stats));stats.generation=generation;stats.target=count;
    bound_seed=g->initial_seed;last_tick=g->tick;
    if(count) {
        g->mode=GARDEN_MODE_BOSS;
        g->boss=(GardenBoss){.x=320000,.y=110000,.target_x=320000,.target_y=110000,
            .hp=50000,.max_hp=50000,.active=1U,.attack=1U,
            .rhythm_state=GARDEN_BOSS_WAIT,.burst_left=3U,.volley_colour=2U};
        for(unsigned i=0U;i<count;i++){populate(&g->enemy_bullets[i],i);stats.replenished++;}
    }
    observe(g);return 0;
}
void g9_workload_before_step(GardenGame *g)
{
    if(!stats.target || !g)return;
    stats.calls++;if(epoch_changed(g))return;
    observe(g);
    if(g->paused){stats.skipped_paused++;return;}
    if(g->mode==GARDEN_MODE_GAMEOVER || g->mode==GARDEN_MODE_VICTORY){stats.skipped_end++;return;}
    stats.steps++;
    for(unsigned i=0U;i<stats.target;i++) {
        GardenEnemyBullet *b=&g->enemy_bullets[i];
        if(!b->active){populate(b,i);stats.replenished++;continue;}
        unsigned wrap=0U;
        if(b->x<20000){b->x=620000;wrap=1U;}
        else if(b->x>620000){b->x=20000;wrap=1U;}
        if(b->y<20000){b->y=460000;wrap=1U;}
        else if(b->y>460000){b->y=20000;wrap=1U;}
        /* Only the stress-injected trajectory wraps. Its velocity,age,TTL,
         * arming and graze flags are retained. Motion's existing >64px guard
         * will snap the visual history at this explicitly logged teleport. */
        stats.teleports+=wrap;
    }
}
void g9_workload_after_step(const GardenGame *g)
{
    if(!stats.target || !g)return;
    observe(g);(void)epoch_changed(g);
}
unsigned g9_workload_get_target(void){return (unsigned)stats.target;}
const G9WorkloadStats *g9_workload_stats(void){return &stats;}
