/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real old/new core comparison and finite risk-driven-demo-bomb regression.
 * No display/MMIO/board claims. The unchanged public state type is shared.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"
void reference_init(GardenGame *,uint32_t);
void reference_step(GardenGame *,uint16_t);
static unsigned checks,runs,wins,manual_steps;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,#x);exit(1); } } while(0)
static int ab(int value) { return value<0 ? -value:value; }

static GardenGame fixture(void)
{
    GardenGame g;garden_init(&g,12345U);g.tick=100U;g.player.grace=0U;
    g.player.recycle=200U;g.demo_mode=1U;g.mode=GARDEN_MODE_BOSS;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=50000,.max_hp=50000,
        .rhythm_state=GARDEN_BOSS_REST,.rhythm_ticks=200U};
    return g;
}
static void shot(GardenGame *g,unsigned i,int dx,int dy,int vx,int vy,int wait)
{
    g->enemy_bullets[i]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,
        .x=g->player.x+dx,.y=g->player.y+dy,.vx=vx,.vy=vy,.ttl=500U,.cord_wait=(int16_t)wait};
}
static unsigned armed(const GardenGame *g)
{
    unsigned n=0U;for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)
        n+=g->enemy_bullets[i].active && g->enemy_bullets[i].cord_wait<0;
    return n;
}
static unsigned ring_alive(const GardenGame *g,unsigned birth)
{
    unsigned n=0U;for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)
        n+=g->enemy_bullets[i].active && g->enemy_bullets[i].age==g->tick-birth+1U;
    return n;
}

static void danger_boundaries(void)
{
    GardenGame old=fixture(),g=old;
    for(unsigned i=0U;i<64U;i++)shot(&g,i,100000,-250000,0,0,79);
    g.enemy_bullet_count=64U;old=g;
    reference_step(&old,0U);garden_step(&g,0U);
    CHECK(old.reward.bombs_used==1U && old.reward.bullets_cleared==64U);
    CHECK(!g.reward.bombs_used && g.enemy_bullet_count==64U && !armed(&g));

    /* One actual activated approach is enough: no global density threshold. */
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.enemy_bullet_count=1U;
    garden_step(&g,0U);
    CHECK(g.reward.bombs_used==1U && g.reward.bullets_cleared==1U && !g.enemy_bullet_count);
    CHECK(g.player.grace==40U && g.reward.bombs==1U && g.boss.hp==50000);
    const struct {int dx,dy,vx,vy,wait;unsigned allowed;} cases[]={
        {0,-60000,0,-6000,-1,0}, /* receding */
        {20000,0,0,0,-1,0},     /* nearby stationary, not approaching */
        {0,-60000,0,6000,20,0}, /* activation cord not ready */
        {120000,0,-16000,0,-1,0}, /* forecast enters, current shot too far */
        {0,-78000,0,6000,-1,0}, /* forecast exactly42px: open boundary */
        {0,-77999,0,6000,-1,1},
        {72000,0,-6000,0,-1,0}, /* forecast exactly36px: open boundary */
        {71999,0,-6000,0,-1,1}
    };
    for(unsigned i=0U;i<sizeof(cases)/sizeof(cases[0]);i++){
        g=fixture();shot(&g,0U,cases[i].dx,cases[i].dy,cases[i].vx,cases[i].vy,cases[i].wait);
        g.enemy_bullet_count=1U;garden_step(&g,0U);
        CHECK(g.reward.bombs_used==cases[i].allowed);
    }
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.player.grace=2U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used && g.player.grace==1U);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.player.grace=1U;garden_step(&g,0U);
    CHECK(g.reward.bombs_used==1U); /* It becomes vulnerable on this logic step. */
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.player.respawning=1U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used && !g.player.respawning);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.player.active=0U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.paused=1U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used && g.tick==100U);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.demo_mode=0U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.mode=GARDEN_MODE_WAVES;
    g.boss.active=0U;garden_step(&g,0U);CHECK(!g.reward.bombs_used);
    /* start_boss discards old wave projectiles after run_player. A forecast
     * computed in WAVES must not become an empty bomb in the new BOSS state.
     */
    g=fixture();g.tick=999U;g.mode=GARDEN_MODE_WAVES;g.boss.active=0U;
    shot(&g,0U,0,-60000,0,6000,-1);g.enemy_bullet_count=1U;
    garden_step(&g,0U);CHECK(g.mode==GARDEN_MODE_BOSS && !g.reward.bombs_used);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.enemy_bullets[0].ttl=1U;
    garden_step(&g,0U);CHECK(!g.enemy_bullet_count && !g.reward.bombs_used);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.reward.bombs_used=1U;g.reward.bombs=1U;
    garden_step(&g,0U);CHECK(g.reward.bombs_used==1U);
    g=fixture();shot(&g,0U,0,-60000,0,6000,-1);g.reward.bombs=0U;garden_step(&g,0U);
    CHECK(!g.reward.bombs_used);
    puts("PASS risk: exact forecast region, active-cord/current-near/approach requirements, density-independent, grace/dead/respawn/pause/manual/wave exclusion, finite one automatic use");
}

static void manual_equivalence(void)
{
    const uint32_t seeds[]={12345U,1U,0xDEADBEEFU};
    for(unsigned i=0U;i<3U;i++){
        GardenGame a,b;garden_init(&a,seeds[i]);reference_init(&b,seeds[i]);
        for(unsigned t=0U;t<5000U;t++){
            /* Both fixtures stay under manual control. Restart's documented
             * default-demo reset is normalized equally before the next step.
             */
            a.demo_mode=b.demo_mode=0U;
            uint16_t input=(t/30U)&1U ? GARDEN_INPUT_LEFT:GARDEN_INPUT_RIGHT;
            if(t%3U==0U)input|=GARDEN_INPUT_SLOW;
            if(t==200U || t==400U || t==600U)input|=GARDEN_INPUT_BOMB;
            if(t==800U || t==810U)input|=GARDEN_INPUT_PAUSE;
            if(t==2500U)input|=GARDEN_INPUT_RESTART;
            garden_step(&a,input);reference_step(&b,input);
            CHECK(!memcmp(&a,&b,sizeof(a)));manual_steps++;
        }
    }
    for(unsigned mode=0U;mode<4U;mode++)for(unsigned condition=0U;condition<4U;condition++){
        GardenGame a=fixture(),b;a.demo_mode=0U;a.mode=(uint8_t)mode;
        if(condition==1U)a.player.grace=100U;
        if(condition==2U)a.player.respawning=1U;
        if(condition==3U)a.player.active=0U;
        shot(&a,0U,100000,0,0,0,-1);a.enemy_bullet_count=1U;b=a;
        garden_step(&a,GARDEN_INPUT_BOMB);reference_step(&b,GARDEN_INPUT_BOMB);
        CHECK(!memcmp(&a,&b,sizeof(a)));
    }
    puts("PASS manual:15000 whole-state old/new equal steps plus all4modes xgrace/respawn/deadQ cases; collision/score/HP/activation unchanged");
}

static void trace_default(unsigned reference)
{
    GardenGame g,b;garden_init(&g,12345U);unsigned birth=0U,used=0U;
    for(unsigned n=0U;n<8000U;n++){
        b=g;if(reference)reference_step(&g,0U);else garden_step(&g,0U);
        if(!birth && g.boss_attack_shots[0]>b.boss_attack_shots[0]){
            birth=g.tick;CHECK(g.enemy_bullet_count==64U);
            CHECK(!armed(&g));printf("TRACE %s first_ring_tick=%u count=%u armed=%u\n",reference?"T_reference":"U_candidate",birth,g.enemy_bullet_count,armed(&g));
        }
        if(birth && !used && (g.tick-birth==1U || g.tick-birth==40U || g.tick-birth==80U || g.tick-birth==120U)){
            printf("TRACE %s ring_age_ticks=%u ring_survivors=%u active_total=%u\n",reference?"T_reference":"U_candidate",g.tick-birth,ring_alive(&g,birth),g.enemy_bullet_count);
            if(!reference && g.tick-birth==1U)CHECK(ring_alive(&g,birth)==64U);
            if(!reference && g.tick-birth<=80U)CHECK(ring_alive(&g,birth)>0U);
        }
        if(g.reward.bombs_used>b.reward.bombs_used){
            int nearest=INT32_MAX,current_x=0,current_y=0,future_x=0,future_y=0;unsigned threats=0U;
            for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++){
                const GardenEnemyBullet *s=&b.enemy_bullets[i];if(!s->active || s->cord_wait>=0)continue;
                int x=b.player.x-s->x,y=b.player.y-s->y,fx=x-s->vx*6,fy=y-s->vy*6;
                if(ab(x)<100000 && ab(y)<100000 && ab(fx)<36000 && ab(fy)<42000 && ab(fx)+ab(fy)<ab(x)+ab(y)){
                    threats++;if(ab(x)+ab(y)<nearest){nearest=ab(x)+ab(y);current_x=x;current_y=y;future_x=fx;future_y=fy;}
                }
            }
            used=1U;
            printf("TRACE %s auto_tick=%u ring_alive_ms=%u before_armed=%u threats=%u current_dx_dy=%d/%d future6_dx_dy=%d/%d cleared=%u previous_count=%u grace_before=%u\n",
                reference?"T_reference":"U_candidate",g.tick,(g.tick-birth)*25U,armed(&b),threats,current_x,current_y,future_x,future_y,g.reward.bullets_cleared,b.enemy_bullet_count,b.player.grace);
            if(reference)CHECK(g.tick-birth==1U && !armed(&b) && !threats);
            else CHECK(g.tick-birth>=80U && armed(&b) && threats && !b.player.grace && !b.player.respawning);
        }
        if(g.tick<b.tick){CHECK(used);return;}
    }
    CHECK(0);
}

static void complete_runs(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    for(unsigned i=0U;i<8U;i++){
        GardenGame g,b;garden_init(&g,seeds[i]);unsigned victory=0U,terminal=0U;
        for(unsigned n=0U;n<10000U;n++){
            b=g;garden_step(&g,0U);
            CHECK(g.enemy_bullet_count<=GARDEN_BOSS_BULLET_BUDGET);
            CHECK(!g.dropped_enemy_bullets && !g.combat.dropped_total);
            CHECK(g.score==g.combat.hit_score_total+g.combat.kill_score_total+g.reward.graze_score_total+g.reward.item_score_total);
            CHECK(g.reward.high_score>=g.score && g.reward.bombs_used<=1U && g.reward.bombs+g.reward.bombs_used==2U);
            if(g.reward.bombs_used>b.reward.bombs_used)CHECK(b.demo_mode && b.mode==GARDEN_MODE_BOSS && b.player.active && !b.player.respawning && b.player.grace<=1U && armed(&b));
            if((g.mode==GARDEN_MODE_VICTORY || g.mode==GARDEN_MODE_GAMEOVER) && !g.end_ticks){
                terminal=1U;victory=g.mode==GARDEN_MODE_VICTORY;wins+=victory;
                printf("RUN seed=%u terminal=%s tick=%u score=%u graze=%u cores=%u bombs=%u hits=%u peak=%u\n",seeds[i],victory?"VICTORY":"GAMEOVER",g.tick,g.score,g.reward.graze_count,g.reward.items_collected,g.reward.bombs_used,g.reward.hits_taken,g.enemy_bullet_peak);
            }
            if(g.tick<b.tick){
                CHECK(terminal && g.reward.high_score>=b.score && g.reward.bombs==2U && !g.score);
                if(!i)CHECK(victory && b.reward.bombs_used==1U && b.reward.items_collected>0U);
                printf("RESTART seed=%u best=%u bombs=%u\n",seeds[i],g.reward.high_score,g.reward.bombs);
                runs++;break;
            }
        }
        CHECK(terminal && runs==i+1U);
    }
}
int main(void)
{
    danger_boundaries();manual_equivalence();trace_default(1U);trace_default(0U);complete_runs();
    printf("RESULT checks=%u runs=%u wins=%u manual_equal_steps=%u state_bytes=%u\n",checks,runs,wins,manual_steps,(unsigned)sizeof(GardenGame));return 0;
}
