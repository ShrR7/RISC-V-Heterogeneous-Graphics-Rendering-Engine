/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent elapsed-microsecond schedules, no board/MMIO or clock sleeping.
 * Event identity links an actual production popup draw to a600ms lifecycle.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_combat.h"
#include "garden_assets.h"

static unsigned checks;
#define CHECK(c,m) do { checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m);exit(2);} } while(0)
typedef struct {
    GardenGame game;
    GardenCombat combat;
    GardenScene scene;
    GardenFeedback feedback;
    GardenFx fx;
} World;
typedef struct {
    uint64_t born_us,last_us,first_us,expired_us,still_start_us,max_bright_still_us;
    int start_x,start_y,last_y;
    unsigned seen,seen_100ms,seen_300ms,last_alpha,rise_budget;
} Trace;

static void begin(World *w)
{
    memset(w,0,sizeof(*w));garden_init(&w->game,12345U);
    memset(w->game.enemies,0,sizeof(w->game.enemies));
    memset(w->game.player_bullets,0,sizeof(w->game.player_bullets));
    memset(w->game.enemy_bullets,0,sizeof(w->game.enemy_bullets));
    memset(w->game.effects,0,sizeof(w->game.effects));
    w->game.tick=1U;w->game.mode=GARDEN_MODE_BOSS;w->game.demo_mode=0U;
    w->game.boss=(GardenBoss){.active=1,.x=320000,.y=121000,.hp=50000,.max_hp=50000,.recycle=500,.phase_ticks=500};
    w->game.player=(GardenPlayer){.active=1,.x=320000,.y=400000,.recycle=500};
    garden_combat_reset(&w->combat);garden_scene_reset(&w->scene);
    garden_feedback_reset(&w->feedback);garden_fx_reset(&w->fx);
    garden_fx_set_flags(&w->fx,GARDEN_FX_FLASH|GARDEN_FX_RING);
}

static void logic_step(World *w,unsigned logical_tick,unsigned hits)
{
    GardenGame *g=&w->game;
    g->tick=logical_tick+1U;g->combat.tick=g->tick;g->combat.count=(uint8_t)hits;
    for(unsigned side=0;side<hits;side++) {
        g->combat.items[side]=(GardenHitEvent){.x=side ? 324000:316000,.y=145000,
            .target_x=320000,.target_y=121000,.score_delta=1U,.damage=95U,
            .target_slot=GARDEN_MAX_ENEMIES,.boss=1U};
        g->score++;g->boss.hp-=95;g->combat.emitted_total++;
        g->combat.hits_total++;g->combat.hit_score_total++;
    }
    GardenGame saved=*g;
    garden_scene_observe(&w->scene,g);garden_feedback_observe(&w->feedback,g);
    garden_fx_observe(&w->fx,g);garden_combat_observe(&w->combat,g);
    CHECK(!memcmp(&saved,g,sizeof(saved)),"event observation leaves all actual game bytes and score unchanged");
}

static void run_schedule(FILE *trace,unsigned rate_num,unsigned rate_den,unsigned burst)
{
    World w;begin(&w);Trace t[24];memset(t,0,sizeof(t));
    GardenDraw draws[GARDEN_DRAW_MAX];uint8_t used[GARDEN_DRAW_MAX];
    uint64_t end_us=burst ? 3600000U:700000U;
    uint64_t frame_interval=(1000000ULL*rate_den+rate_num-1U)/rate_num;
    unsigned simulated=0U,started=0U;
    for(unsigned frame=0;;frame++) {
        uint64_t us=(uint64_t)frame*1000000U*rate_den/rate_num;
        if(us>end_us) break;
        unsigned desired=(unsigned)(us/25000U);
        if(!started) { logic_step(&w,0U,2U);started=1U; }
        while(simulated<desired) {
            simulated++;
            unsigned hits=burst && simulated<120U && simulated%10U==0U ? 2U:0U;
            logic_step(&w,simulated,hits);
        }
        unsigned fraction=(unsigned)(us%25000U)*256U/25000U;
        garden_combat_set_render_fraction(&w.combat,fraction);
        GardenGame saved=w.game;
        unsigned n=garden_combat_build(&w.combat,&w.scene,&w.feedback,&w.fx,&w.game,draws,GARDEN_DRAW_MAX,1U,1U);
        CHECK(!memcmp(&saved,&w.game,sizeof(saved)),"drawing and birth layout preserve complete actual game bytes");
        memset(used,0,sizeof(used));unsigned visible=0U;
        for(unsigned index=0;index<GARDEN_COMBAT_POPUP_MAX;index++) {
            const GardenCombatPopup *p=&w.combat.popups[index];GardenDraw d;
            if(!garden_combat_popup_draw(&w.combat,index,1U,&d)) continue;
            unsigned seq=p->spawn_sequence;
            CHECK(seq<24U,"three-second repeated dual hits use at most24 real identities");
            Trace *v=&t[seq];uint64_t born_us=(uint64_t)(seq/2U)*250000U;
            CHECK(us>=born_us && us-born_us<600000U,"each visible identity is younger than exactly600ms of active engine time");
            CHECK((d.asset&0x7FFFU)==95U && d.x>=2 && d.x+44<=638 && d.y>=2 && d.y+16<=478,
                  "independent true95 label stays in its complete birth-reserved field path");
            if(!v->seen) {
                v->seen=1U;v->born_us=born_us;v->first_us=us;v->start_x=d.x;v->start_y=p->y-8;
                v->last_y=d.y;v->last_alpha=d.alpha;v->still_start_us=us;v->rise_budget=p->rise_budget;
                CHECK(us-born_us<=frame_interval,"first visible birth occurs within one real frame of its own event");
                CHECK(d.alpha>=31U,"full-Alpha birth pop is visible before fading");
            }
            CHECK(d.x==v->start_x && d.y<=v->last_y && d.alpha<=v->last_alpha,
                  "identity retains one horizontal anchor, monotonic ease-out travel and independent fade");
            CHECK(v->start_y-d.y<=(int)v->rise_budget && p->rise_budget==v->rise_budget,"complete easing path never exceeds or recomputes its own birth rise budget");
            if(d.y!=v->last_y) v->still_start_us=us;
            else if(d.alpha>=8U && us-v->still_start_us>v->max_bright_still_us) v->max_bright_still_us=us-v->still_start_us;
            uint64_t age_us=us-born_us;
            if(!v->seen_100ms && age_us>=100000U) {
                CHECK((v->start_y-d.y)*10+5>=(int)v->rise_budget*3,"first100ms pop reaches at least30percent of its fixed rise budget within half-pixel rounding");v->seen_100ms=1U;
            }
            if(!v->seen_300ms && age_us>=300000U) {
                CHECK((v->start_y-d.y)*4+2>=(int)v->rise_budget*3,"first300ms reaches at least75percent of its fixed rise budget within half-pixel rounding");v->seen_300ms=1U;
            }
            unsigned match=0U;
            for(unsigned i=0;i<n;i++) if(!used[i] && draws[i].x==d.x && draws[i].y==d.y && draws[i].asset==d.asset && draws[i].alpha==d.alpha) {used[i]=1U;match=1U;break;}
            CHECK(match,"each identity draw exists as a separate admitted composite in the actual production draw list");
            fprintf(trace,"{\"rate_num\":%u,\"rate_den\":%u,\"burst\":%u,\"us\":%llu,\"seq\":%u,\"birth_us\":%llu,\"x\":%d,\"y\":%d,\"alpha\":%u,\"age_ticks\":%u,\"fraction_q8\":%u,\"rise_budget\":%u,\"anchor_policy\":%u}\n",
                rate_num,rate_den,burst,(unsigned long long)us,seq,(unsigned long long)born_us,d.x,d.y,d.alpha,p->age,fraction,p->rise_budget,p->anchor_policy);
            v->last_us=us;v->last_y=d.y;v->last_alpha=d.alpha;visible++;
        }
        CHECK(w.combat.stats.popups==visible,"every anchored identity is admitted at the normal production draw budget");
        for(unsigned seq=0;seq<(burst ? 24U:2U);seq++) {
            Trace *v=&t[seq];
            if(v->seen && !v->expired_us && us-v->born_us>=600000U) v->expired_us=us;
        }
    }
    unsigned expected=burst ? 24U:2U;
    for(unsigned seq=0;seq<expected;seq++) {
        const Trace *v=&t[seq];
        CHECK(v->seen && v->seen_100ms && v->seen_300ms,"every identity was independently tracked through its full moving lifetime");
        CHECK(v->expired_us-v->born_us>=600000U && v->expired_us-v->born_us<=600000U+frame_interval,
              "expiry is exactly600ms, observed within one actual60/57.6/32.7Hz frame");
        CHECK(v->rise_budget==31U && v->start_y-v->last_y==(int)v->rise_budget && v->last_alpha<=2U,"121px Boss labels finish their complete31px head path and independently fade nearzero");
        CHECK(v->max_bright_still_us<=75000U,"no bright label is pinned to a top/bar boundary for a long interval");
    }
    CHECK(t[0].start_x==298 && t[1].start_x==298 && t[0].start_y==33 && t[1].start_y==33,"each actual dual hit independently overlaps at the exact centred Boss head anchor");
    CHECK(w.combat.stats.popups_spawned==expected && w.combat.stats.popup_anchors_chosen==expected &&
          w.combat.stats.damage_observed==expected*95U && w.combat.stats.score_observed==expected,
          "each event births/anchors independently, with full real damage and score ledgers");
    CHECK(!w.combat.stats.popups_merged && !w.combat.stats.popups_updated &&
          !w.combat.stats.popup_groups_replaced && !w.combat.stats.popup_pool_exhausted &&
          !w.combat.stats.popup_anchor_failures && !w.combat.stats.event_gaps,
          "ordinary real-time repeated hits cause no merging, refreshing, pool loss or failed birth paths");
    printf("PASS: elapsed-us %u/%uHz %s identities=%u,600ms expiry within1frame,bounded31px head ease-out,>=30percent/100ms,stable centred anchors\n",
        rate_num,rate_den,burst ? "3second repeated dual-hit stream":"isolated dual-hit",expected);
}

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FILE *trace=fopen(argv[1],"wb");CHECK(trace!=NULL,"trace file opened");
    run_schedule(trace,60U,1U,0U);run_schedule(trace,576U,10U,0U);run_schedule(trace,327U,10U,0U);
    run_schedule(trace,60U,1U,1U);run_schedule(trace,576U,10U,1U);run_schedule(trace,327U,10U,1U);
    CHECK(!fclose(trace),"trace file closed without loss");
    printf("PASS: independent popup animation timestamp checks=%u\n",checks);return 0;
}
