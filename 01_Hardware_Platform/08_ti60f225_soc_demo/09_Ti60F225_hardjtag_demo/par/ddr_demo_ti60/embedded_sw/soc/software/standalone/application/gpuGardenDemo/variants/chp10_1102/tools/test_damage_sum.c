/* SPDX-License-Identifier: GPL-3.0-or-later
 * Same-logic-tick damage labels: real production modules, host RAM only.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_combat.h"
#include "garden_assets.h"
#include "garden_actor_ui.h"

static unsigned checks,real_batches,stream_steps;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)

typedef struct {
    GardenGame g;
    GardenCombat c;
    GardenScene s;
    GardenFeedback f;
    GardenFx fx;
} World;

static void begin(World *w)
{
    memset(w,0,sizeof(*w)); garden_init(&w->g,12345U);
    memset(w->g.enemies,0,sizeof(w->g.enemies));
    memset(w->g.player_bullets,0,sizeof(w->g.player_bullets));
    memset(w->g.enemy_bullets,0,sizeof(w->g.enemy_bullets));
    memset(w->g.effects,0,sizeof(w->g.effects));
    memset(&w->g.boss,0,sizeof(w->g.boss));
    w->g.tick=100U; w->g.combat.tick=w->g.tick;
    w->g.mode=GARDEN_MODE_BOSS; w->g.demo_mode=0U;
    w->g.player=(GardenPlayer){.active=1,.x=320000,.y=410000,.recycle=500,.grace=80};
    garden_combat_reset(&w->c); garden_scene_reset(&w->s);
    garden_feedback_reset(&w->f); garden_fx_reset(&w->fx);
    garden_combat_observe(&w->c,&w->g);
    garden_scene_observe(&w->s,&w->g); garden_feedback_observe(&w->f,&w->g);
    garden_fx_observe(&w->fx,&w->g);
}

static unsigned active(const GardenCombat *c)
{ unsigned n=0; for(unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)n+=c->popups[i].active!=0U;return n; }

static unsigned sum(const GardenCombat *c)
{ unsigned n=0;for(unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)if(c->popups[i].active)n+=c->popups[i].damage;return n; }

static void observe(World *w)
{
    GardenGame saved=w->g;
    garden_combat_observe(&w->c,&w->g); garden_scene_observe(&w->s,&w->g);
    garden_feedback_observe(&w->f,&w->g); garden_fx_observe(&w->fx,&w->g);
    CHECK(!memcmp(&saved,&w->g,sizeof(saved)),"production observers preserve complete core game state");
}

static unsigned build(World *w,GardenDraw *d)
{
    GardenGame saved=w->g;
    unsigned n=garden_combat_build(&w->c,&w->s,&w->f,&w->fx,&w->g,d,GARDEN_DRAW_MAX,1U,1U);
    CHECK(!memcmp(&saved,&w->g,sizeof(saved)),"production builder preserves complete core state");
    CHECK(n<=GARDEN_DRAW_MAX,"bounded production draw admission");return n;
}

static unsigned count_asset(const GardenDraw *d,unsigned n,unsigned asset)
{ unsigned k=0;for(unsigned i=0;i<n;i++)k+=d[i].asset==asset;return k; }

static unsigned popup_for(const GardenCombat *c,unsigned slot,unsigned damage)
{
    for(unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)
        if(c->popups[i].active && c->popups[i].slot==slot && c->popups[i].damage==damage)return i;
    return GARDEN_COMBAT_POPUP_MAX;
}

static GardenHitEvent event(unsigned slot,unsigned damage)
{
    unsigned boss=slot==GARDEN_MAX_ENEMIES;
    return (GardenHitEvent){.x=320000,.y=150000,.target_x=320000,.target_y=150000,
        .score_delta=1U,.damage=(uint16_t)damage,.target_slot=(uint8_t)slot,
        .target_type=(uint8_t)(boss?GARDEN_ENEMY_NONE:GARDEN_ENEMY_STOPPER),.boss=(uint8_t)boss};
}

static void batch(World *w,const GardenHitEvent *e,unsigned n)
{
    ++w->g.tick;w->g.combat.tick=w->g.tick;w->g.combat.count=(uint8_t)n;
    for(unsigned i=0;i<n && i<GARDEN_COMBAT_EVENT_MAX;i++)w->g.combat.items[i]=e[i];
    w->g.combat.emitted_total+=n;observe(w);
}

static void add_target(World *w,unsigned slot,unsigned type,int hp,int x,int y)
{
    if(slot==GARDEN_MAX_ENEMIES)
        w->g.boss=(GardenBoss){.active=1,.x=x,.y=y,.hp=hp,.max_hp=hp,
            .target_x=x,.move_ticks=100,.phase_ticks=100};
    else
        w->g.enemies[slot]=(GardenEnemy){.active=1,.type=(uint8_t)type,.phase=2,
            .target_time=150,.recycle=100,.x=x,.y=y,.hp=hp};
}

static void shoot(World *w,unsigned bullet,unsigned slot,unsigned damage,int dx)
{
    int x,y;
    if(slot==GARDEN_MAX_ENEMIES){x=w->g.boss.x;y=w->g.boss.y;}
    else{x=w->g.enemies[slot].x;y=w->g.enemies[slot].y+(w->g.enemies[slot].type==GARDEN_ENEMY_DARTER?4000:-30000);}
    w->g.player_bullets[bullet]=(GardenPlayerBullet){.active=1,.x=x+dx,.y=y+14000,.vy=-14000,.damage=(uint16_t)damage};
}

static void actual_two_hits(void)
{
    for(unsigned kind=0;kind<3;kind++)for(unsigned lethal=0;lethal<2;lethal++){
        World w;GardenDraw d[GARDEN_DRAW_MAX];begin(&w);
        unsigned slot=kind==2?GARDEN_MAX_ENEMIES:0U;
        unsigned type=kind==0?GARDEN_ENEMY_DARTER:GARDEN_ENEMY_STOPPER;
        int hp=lethal?100:1000;
        add_target(&w,slot,type,hp,320000,180000);
        shoot(&w,0,slot,95,-4000);shoot(&w,1,slot,95,4000);
        garden_step(&w.g,0);observe(&w);real_batches++;
        unsigned damage=lethal?100U:190U;
        unsigned reward=lethal?(kind==2?1000U:kind==0?20U:80U):0U;
        CHECK(w.g.combat.count==2,"real two projectiles emit two independent effective-hit events");
        CHECK(w.g.combat.items[0].damage==95 && w.g.combat.items[1].damage==(lethal?5U:95U),"effective second hit excludes overkill");
        CHECK(w.g.combat.hits_total==2 && w.g.score==reward+2U,"real hit score and kill reward unchanged");
        CHECK(w.c.stats.events_observed==2 && w.c.stats.damage_observed==damage && w.c.stats.score_observed==reward+2U,"all original event and HP ledgers remain separate");
        CHECK(w.c.stats.popups_spawned==1 && w.c.stats.popups_merged==1 && active(&w.c)==1 && sum(&w.c)==damage,"same target/tick is one summed damage label");
        CHECK(w.c.stats.impacts_spawned==2 && w.c.stats.kill_rings_spawned==lethal,"per-hit impacts and lethal ring remain event-driven");
        unsigned idx=popup_for(&w.c,slot,damage);CHECK(idx<GARDEN_COMBAT_POPUP_MAX,"actual effective sum is present");
        CHECK(w.c.popups[idx].killed==lethal,"combined label retains killing flag");
        unsigned n=build(&w,d);
        CHECK(count_asset(d,n,GARDEN_DRAW_SCORE_BASE+damage)==1,"draw list has one actual combined value");
        GardenCombat saved=w.c;garden_combat_observe(&w.c,&w.g);
        CHECK(!memcmp(&saved,&w.c,sizeof(saved)),"same-tick observation is bytewise idempotent");
        if(!lethal){
            shoot(&w,0,slot,95,0);garden_step(&w.g,0);observe(&w);
            CHECK(active(&w.c)==2 && sum(&w.c)==285 && w.c.stats.popups_spawned==2 && w.c.stats.popups_merged==1,"next tick births a separate label without refreshing prior sum");
        }
    }
    puts("REAL shots: both enemy kinds/Boss, 95+95=190, capped lethal95+5=100, later tick separate");
}

static void actual_blast_and_shot(void)
{
    World w;begin(&w);add_target(&w,0,GARDEN_ENEMY_STOPPER,2000,200000,180000);
    shoot(&w,0,0,95,0);
    w.g.ordnance=(GardenBomb){.active=1,.x=200000,.y=189000};
    garden_step(&w.g,0);observe(&w);real_batches++;
    CHECK(w.g.combat.count==2 && w.g.combat.items[0].damage==600 && w.g.combat.items[1].damage==95,"real blast and shot remain independently consumed");
    CHECK(w.g.enemies[0].hp==1305 && w.g.score==2 && w.g.weapon_stats.bomb_damage==600,"actual blast, HP and score unchanged");
    CHECK(active(&w.c)==1 && sum(&w.c)==695 && w.c.stats.popups_merged==1 && w.c.stats.damage_observed==695,"same tick mixed weapon display sums effective695");
}

static void identity_and_invalid(void)
{
    World w;begin(&w);
    GardenHitEvent e[6]={event(0,15),event(1,95),event(8,95),event(0,20),event(1,95),event(8,95)};
    batch(&w,e,6);
    CHECK(active(&w.c)==3 && w.c.stats.popups_spawned==3 && w.c.stats.popups_merged==3,"interleaved target slots each form their own group");
    CHECK(popup_for(&w.c,0,35)<GARDEN_COMBAT_POPUP_MAX && popup_for(&w.c,1,190)<GARDEN_COMBAT_POPUP_MAX && popup_for(&w.c,8,190)<GARDEN_COMBAT_POPUP_MAX,"enemy and Boss values never cross targets");
    begin(&w);e[0]=event(0,95);e[1]=event(0,95);e[1].target_type=GARDEN_ENEMY_DARTER;
    batch(&w,e,2);CHECK(active(&w.c)==2 && !w.c.stats.popups_merged,"different target types cannot merge even in malformed lifecycle stream");
    begin(&w);
    GardenHitEvent bad[12];bad[0]=event(8,95);bad[11]=event(8,95);
    for(unsigned i=1;i<11;i++){
        bad[i]=event(8,95);
        switch(i){
        case 1:bad[i].damage=0;break;case 2:bad[i].target_slot=9;break;
        case 3:bad[i].boss=2;break;case 4:bad[i].boss=0;break;
        case 5:bad[i].target_type=GARDEN_ENEMY_DARTER;break;case 6:bad[i].killed=2;break;
        case 7:bad[i].kill_reward=1000;break;case 8:bad[i].score_delta=2;break;
        case 9:bad[i].score_delta=32768;break;default:bad[i].target_type=255;break;
        }
    }
    batch(&w,bad,12);
    CHECK(w.c.stats.events_observed==2 && w.c.stats.event_rejected==10 && w.c.stats.damage_observed==190,"malformed records rejected before display summation");
    CHECK(active(&w.c)==1 && sum(&w.c)==190 && w.c.stats.popups_merged==1,"invalid events neither create nor inflate sum");
    begin(&w);GardenHitEvent oversized[GARDEN_COMBAT_EVENT_MAX];
    for(unsigned i=0;i<GARDEN_COMBAT_EVENT_MAX;i++)oversized[i]=event(8,1);
    batch(&w,oversized,255);
    CHECK(w.c.stats.event_rejected==255-GARDEN_COMBAT_EVENT_MAX && w.c.stats.events_observed==GARDEN_COMBAT_EVENT_MAX && sum(&w.c)==GARDEN_COMBAT_EVENT_MAX,"255-count bounds do not read or merge outside33-event storage");
}

static void pool_pressure_and_overflow(void)
{
    for(unsigned wrap=0;wrap<2;wrap++){
        World w;begin(&w);
        w.c.stats.popups_spawned=wrap?UINT32_MAX-3U:GARDEN_COMBAT_POPUP_MAX;
        for(unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)w.c.popups[i]=(GardenCombatPopup){
            .active=1,.age=10,.slot=8,.target_type=GARDEN_ENEMY_NONE,.damage=1,
            .target_x=320000,.target_y=150000,
            .spawn_sequence=w.c.stats.popups_spawned-GARDEN_COMBAT_POPUP_MAX+i};
        GardenHitEvent e[GARDEN_COMBAT_EVENT_MAX];unsigned totals[GARDEN_MAX_ENEMIES+1]={0};
        for(unsigned i=0;i<GARDEN_COMBAT_EVENT_MAX;i++){unsigned slot=i%(GARDEN_MAX_ENEMIES+1U);e[i]=event(slot,95);totals[slot]+=95;}
        batch(&w,e,GARDEN_COMBAT_EVENT_MAX);
        CHECK(active(&w.c)==GARDEN_COMBAT_POPUP_MAX && w.c.stats.popup_pool_exhausted==9 && w.c.stats.popup_replaced==9,"full pool replaces only one oldest label for each of nine new targets");
        CHECK(w.c.stats.popups_merged==GARDEN_COMBAT_EVENT_MAX-9 && w.c.stats.damage_observed==95U*GARDEN_COMBAT_EVENT_MAX,"pool pressure preserves every true hit and merge count");
        for(unsigned slot=0;slot<=GARDEN_MAX_ENEMIES;slot++)CHECK(popup_for(&w.c,slot,totals[slot])<GARDEN_COMBAT_POPUP_MAX,"current group ownership survives interleaving and sequence wrap");
    }
    World w;begin(&w);GardenHitEvent e[3]={event(8,30000),event(8,30000),event(8,10000)};
    batch(&w,e,3);
    CHECK(sum(&w.c)==32767 && active(&w.c)==1 && w.c.stats.damage_observed==70000,"display cap does not wrap or truncate real observed damage");
    CHECK(w.c.stats.damage_display_clamped==70000U-32767U && w.c.stats.popups_merged==2,"each newly omitted damage unit counted once");
}

static void time_toggle_epoch(void)
{
    World w;begin(&w);GardenHitEvent e[2]={event(8,95),event(8,95)};batch(&w,e,2);
    GardenCombat saved=w.c;w.g.paused=1;garden_step(&w.g,0);observe(&w);
    CHECK(!memcmp(&saved,&w.c,sizeof(saved)),"pause freezes age and cannot replay prior two-hit batch");
    w.g.paused=0;garden_combat_set_enabled(&w.c,0);batch(&w,e,2);
    CHECK(!active(&w.c) && w.c.stats.events_observed==4 && w.c.stats.popups_spawned==1,"disabled presentation consumes individual events without labels");
    garden_combat_set_enabled(&w.c,1);garden_combat_observe(&w.c,&w.g);
    CHECK(!active(&w.c),"re-enable same tick never replays consumed events");
    batch(&w,e,2);CHECK(active(&w.c)==1 && sum(&w.c)==190,"next fresh tick resumes summed display");
    garden_step(&w.g,GARDEN_INPUT_RESTART);observe(&w);
    CHECK(!active(&w.c) && w.g.tick==0 && w.g.combat.count==0,"restart rollback clears visuals and no prior damage reappears");
    batch(&w,e,2);CHECK(active(&w.c)==1 && sum(&w.c)==190,"fresh epoch new events sum normally");
    w.g.tick+=GARDEN_COMBAT_POPUP_TICKS;w.g.combat.tick=w.g.tick;w.g.combat.count=0;observe(&w);
    CHECK(!active(&w.c),"summed popup expires at original24tick/600ms lifetime");
}

static void anchor_animation(void)
{
    World w;GardenDraw d[GARDEN_DRAW_MAX];begin(&w);
    add_target(&w,8,0,5000,320000,250000);
    GardenHitEvent e[2]={event(8,95),event(8,95)};
    e[0].target_y=e[1].target_y=250000;
    batch(&w,e,2);build(&w,d);unsigned idx=popup_for(&w.c,8,190);
    CHECK(idx<GARDEN_COMBAT_POPUP_MAX,"merged label available for draw identity");
    const GardenCombatPopup *p=&w.c.popups[idx];
    CHECK(p->anchor_ready && p->anchor_visible && p->anchor_policy==1 && p->x==320,"one head-centred birth anchor for combined width");
    CHECK(w.c.stats.popup_anchors_chosen==1 && !w.c.stats.popup_anchor_failures,"only one anchor chosen for sum");
    GardenDraw start,last;CHECK(garden_combat_popup_draw(&w.c,idx,1,&start),"actual production draw readout exists");last=start;
    int16_t anchor_x=p->x,anchor_y=p->y;uint32_t seq=p->spawn_sequence;
    for(unsigned age=1;age<24;age++){
        ++w.g.tick;w.g.combat.tick=w.g.tick;w.g.combat.count=0;observe(&w);
        garden_combat_set_render_fraction(&w.c,127);
        build(&w,d);GardenDraw now;
        CHECK(garden_combat_popup_draw(&w.c,idx,1,&now),"combined label independently animates before expiry");
        CHECK(now.asset==GARDEN_DRAW_SCORE_BASE+190 && now.x==start.x && now.y<=last.y && now.alpha<=last.alpha,"sum value stable, rises monotonically and fades");
        CHECK(w.c.popups[idx].x==anchor_x && w.c.popups[idx].y==anchor_y && w.c.popups[idx].spawn_sequence==seq,"birth identity and fixed anchor never reset during subsequent frames");
        last=now;
    }
    CHECK(start.y-last.y>=24 && last.alpha<=2,"32px rise and late fade remain visible for summed value");
}

static void snapshot_stream(void)
{
    for(unsigned seed=1;seed<=8;seed++){
        World w;begin(&w);GardenGame reference;
        garden_init(&w.g,seed*0x1020301U);reference=w.g;
        garden_combat_reset(&w.c);garden_scene_reset(&w.s);garden_feedback_reset(&w.f);garden_fx_reset(&w.fx);
        for(unsigned step=0;step<3000;step++){
            uint16_t input=step==777U || step==783U?GARDEN_INPUT_PAUSE:step==2333U?GARDEN_INPUT_RESTART:0;
            garden_step(&w.g,input);garden_step(&reference,input);GardenGame saved=w.g;
            uint32_t before_events=w.c.stats.events_observed,before_spawn=w.c.stats.popups_spawned,before_merge=w.c.stats.popups_merged;
            observe(&w);GardenDraw d[GARDEN_DRAW_MAX];build(&w,d);
            CHECK(!memcmp(&reference,&w.g,sizeof(reference)) && !memcmp(&saved,&w.g,sizeof(saved)),"24000 natural production steps preserve full game bytes through observer/build and pause/restart");
            CHECK(w.c.stats.events_observed-before_events==(w.c.stats.popups_spawned-before_spawn)+(w.c.stats.popups_merged-before_merge),"every accepted observed hit contributes exactly one birth or same-tick merge");
            CHECK(!w.c.stats.event_rejected && !w.c.stats.damage_display_clamped,"natural game emits valid effective HP damage");
            stream_steps++;
        }
    }
}

int main(void)
{
    actual_two_hits();actual_blast_and_shot();identity_and_invalid();pool_pressure_and_overflow();
    time_toggle_epoch();anchor_animation();snapshot_stream();
    printf("PASS damage_sum checks=%u real_batches=%u stream_steps=%u popup_bytes=%u combat_bytes=%u pool=%u\n",
        checks,real_batches,stream_steps,(unsigned)sizeof(GardenCombatPopup),(unsigned)sizeof(GardenCombat),GARDEN_COMBAT_POPUP_MAX);
    return 0;
}
