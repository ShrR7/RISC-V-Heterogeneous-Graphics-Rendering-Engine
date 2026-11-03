/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual event-driven production presentation contracts; no MMIO.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_combat.h"
#include "garden_assets.h"
#include "garden_actor_ui.h"

static unsigned checks;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)

static void begin(GardenGame *g,GardenCombat *c,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{
    garden_init(g,12345U);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->player_bullets,0,sizeof(g->player_bullets));
    memset(g->enemy_bullets,0,sizeof(g->enemy_bullets));
    memset(g->effects,0,sizeof(g->effects));
    memset(&g->boss,0,sizeof(g->boss));
    g->tick=100U; g->mode=GARDEN_MODE_BOSS; g->demo_mode=0U;
    g->player=(GardenPlayer){.active=1,.x=320000,.y=400000,.recycle=500};
    garden_combat_reset(c); garden_scene_reset(s);
    garden_feedback_reset(f); garden_fx_reset(fx);
    garden_combat_observe(c,g); garden_scene_observe(s,g);
    garden_feedback_observe(f,g); garden_fx_observe(fx,g);
}

static unsigned active_popup(const GardenCombat *c)
{
    unsigned n=0U;
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) n+=c->popups[i].active!=0U;
    return n;
}

static unsigned popup_sum(const GardenCombat *c)
{
    unsigned n=0U;
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) if(c->popups[i].active) n+=c->popups[i].damage;
    return n;
}

static unsigned asset_count(const GardenDraw *d,unsigned n,unsigned asset)
{
    unsigned found=0U;
    for (unsigned i=0;i<n;i++) found+=d[i].asset==asset;
    return found;
}

static unsigned score_width(unsigned value)
{
    unsigned digits=1U;
    while(value>=10U) { value/=10U; digits++; }
    return 12U+digits*16U;
}

static void check_score_layout(const GardenGame *g,const GardenDraw *draws,unsigned count)
{
    unsigned shown=0U;(void)g;
    for(unsigned i=0;i<count;i++) {
        const GardenDraw *d=&draws[i];
        if(d->asset<GARDEN_DRAW_SCORE_BASE) continue;
        unsigned width=score_width(d->asset-GARDEN_DRAW_SCORE_BASE);shown++;
        CHECK(d->x>=2 && d->x+(int)width<=638 && d->y>=2 && d->y+16<=478,
              "every admitted head label stays within its once-bounded field path");
    }
    CHECK(shown<=GARDEN_COMBAT_POPUP_MAX,"only32 independent head labels reach presentation");
}

static void observe_all(GardenCombat *c,GardenScene *s,GardenFeedback *f,GardenFx *fx,const GardenGame *g)
{
    garden_combat_observe(c,g); garden_scene_observe(s,g);
    garden_feedback_observe(f,g); garden_fx_observe(fx,g);
}

static GardenHitEvent hit(unsigned slot,unsigned type,unsigned killed,unsigned credited)
{
    unsigned boss=slot==GARDEN_MAX_ENEMIES;
    return (GardenHitEvent){.x=321000,.y=146000,.target_x=320000,.target_y=120000,
        .score_delta=credited,.kill_reward=killed ? boss ? 1000U : type==GARDEN_ENEMY_DARTER ? 20U : 80U : 0U,
        .damage=95,.target_slot=(uint8_t)slot,.target_type=(uint8_t)type,
        .killed=(uint8_t)killed,.boss=(uint8_t)boss};
}

static void events(GardenGame *g,const GardenHitEvent *items,unsigned count)
{
    g->tick++; g->combat.tick=g->tick; g->combat.count=(uint8_t)count;
    for(unsigned i=0;i<count && i<GARDEN_MAX_PLAYER_BULLETS;i++) g->combat.items[i]=items[i];
    g->combat.emitted_total+=count;
}

static void advance(GardenGame *g,GardenCombat *c,unsigned ticks)
{
    g->tick+=ticks; g->combat.tick=g->tick; g->combat.count=0U;
    garden_combat_observe(c,g);
}

static void actual_events(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[GARDEN_DRAW_MAX];
    begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=190,.max_hp=190,.recycle=100,.phase_ticks=100};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=95};
    garden_step(&g,0U); saved=g; observe_all(&c,&s,&f,&fx,&g);
    CHECK(g.score==1U && g.boss.hp==95 && g.combat.count==1U,"production core awards +1 for actual damaging Boss hit");
    CHECK(c.stats.events_observed==1U && c.stats.score_observed==1U && !c.stats.event_rejected,"explicit event is consumed exactly once");
    CHECK(c.impacts[0].x==321 && c.impacts[0].y==146 && c.flash[8].remaining==6U,"true consumed bullet coordinates and held flash retained");
    CHECK(active_popup(&c)==1U && popup_sum(&c)==95U,"actual95 HP reduction creates damage group, independent of credited+1");
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,GARDEN_DRAW_MAX,1U,1U);
    CHECK(asset_count(d,n,GARDEN_ASSET_FX_RING32)==1U && asset_count(d,n,GARDEN_ASSET_HIT_SPARK)==1U,"large layered impact paints");
    CHECK(asset_count(d,n,GARDEN_DRAW_SCORE_BASE+95U)==1U,"one composite damage history entry");
    CHECK(d[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"collision marker is last over impacts and projectiles");
    CHECK(!memcmp(&saved,&g,sizeof(g)),"observer and builder preserve complete game including score/event schema");
    GardenCombat same=c;
    garden_combat_observe(&c,&g);
    CHECK(!memcmp(&same,&c,sizeof(c)),"same-tick observation never duplicates score or ages");
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=323000,.y=160000,.vy=-14000,.damage=95};
    garden_step(&g,0U); saved=g; observe_all(&c,&s,&f,&fx,&g);
    CHECK(g.mode==GARDEN_MODE_VICTORY && g.score==1002U && g.combat.items[0].score_delta==1001U,"production Boss kill retains1000 reward plus hit point");
    CHECK(c.stats.events_observed==2U && c.stats.kills_observed==1U && c.stats.score_observed==1002U,"terminal victory event not lost on mode transition");
    CHECK(active_popup(&c)==2U && popup_sum(&c)==190U && !c.stats.popups_merged && !c.stats.popups_updated,"killing hit95 births independent label without modifying prior hit95 or including1000 reward");
    n=garden_combat_build(&c,&s,&f,&fx,&g,d,GARDEN_DRAW_MAX,1U,1U);
    CHECK(c.stats.kill_rings==1U && asset_count(d,n,GARDEN_DRAW_SCORE_BASE+95U)==2U,"victory keeps kill shock and both true95 hit labels");
    CHECK(!memcmp(&saved,&g,sizeof(g)),"terminal observer/build cannot modify any core state");
    GardenCombat late; garden_combat_reset(&late); garden_combat_observe(&late,&g);
    CHECK(late.stats.events_observed==1U && popup_sum(&late)==95U,"first observation consumes only current fresh HP damage");
    for (unsigned type=GARDEN_ENEMY_DARTER;type<=GARDEN_ENEMY_STOPPER;type++) {
        begin(&g,&c,&s,&f,&fx);
        g.enemies[0]=(GardenEnemy){.active=1,.type=(uint8_t)type,.phase=2,.recycle=100,.target_time=100,
            .x=200000,.y=150000,.hp=95};
        int32_t impact_y=type==GARDEN_ENEMY_DARTER ? 154000 : 120000;
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=201000,.y=impact_y+14000,.vy=-14000,.damage=95};
        garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
        unsigned reward=type==GARDEN_ENEMY_DARTER ? 20U : 80U;
        CHECK(!g.enemies[0].active && g.score==reward+1U,"both original enemy rewards plus actual hit point");
        CHECK(c.stats.events_observed==1U && c.stats.kills_observed==1U && popup_sum(&c)==95U,"enemy kill shows effective95 damage without20/80 rewards");
        CHECK(c.impacts[0].x==201 && c.impacts[0].y==impact_y/1000,"actual enemy impact remains bullet-anchored");
    }
    puts("PASS: production Boss/enemy hit and kill events, credited scores, terminal effects, complete-state purity");
}

static void freshness_credit_and_invalid(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    begin(&g,&c,&s,&f,&fx);
    g.tick++; g.score=999U; g.boss.hp=-50; g.boss.flash=3;
    garden_combat_observe(&c,&g);
    CHECK(!c.stats.events_observed && !active_popup(&c),"HP/flash/score changes without explicit events never invent score feedback");
    GardenHitEvent e=hit(8U,0U,0U,1U);
    events(&g,&e,1U); g.combat.tick--;
    garden_combat_observe(&c,&g);
    CHECK(!c.stats.events_observed && c.stats.event_gaps==1U,"stale event tick cannot replay");
    events(&g,&e,1U); garden_combat_observe(&c,&g);
    CHECK(c.stats.events_observed==1U && popup_sum(&c)==95U,"next fresh damage event resumes normally");
    advance(&g,&c,24U);
    CHECK(!active_popup(&c),"24tick popup expires exactly");
    e.score_delta=0U; events(&g,&e,1U); garden_combat_observe(&c,&g);
    CHECK(c.stats.events_observed==2U && c.impacts[0].active && popup_sum(&c)==95U,"score saturation+0 still presents real95 HP damage");
    e=hit(8U,0U,1U,23U); events(&g,&e,1U); garden_combat_observe(&c,&g);
    CHECK(popup_sum(&c)==190U && c.stats.kills_observed==1U && c.kill_rings[0].active && c.stats.score_observed==24U,"partial saturated kill credit23 remains score audit only;95damage killing label independent");
    unsigned before=c.stats.events_observed,rejected=c.stats.event_rejected;
    for(unsigned reason=0;reason<9U;reason++) {
        e=hit(8U,0U,0U,1U);
        switch(reason) {
            case 0: e.damage=0U; break;
            case 1: e.target_slot=9U; break;
            case 2: e.boss=2U; break;
            case 3: e.boss=0U; break;
            case 4: e.target_type=GARDEN_ENEMY_DARTER; break;
            case 5: e.killed=2U; break;
            case 6: e.kill_reward=1000U; break;
            case 7: e.score_delta=2U; break;
            default: e.killed=1U; e.kill_reward=20U; e.score_delta=21U; break;
        }
        events(&g,&e,1U); garden_combat_observe(&c,&g);
    }
    CHECK(c.stats.events_observed==before && c.stats.event_rejected==rejected+9U,"invalid slot/type/boss/zero damage/reward/over-credit rejected");
    puts("PASS: freshness, no invented score, exact zero/partial saturation credit, malformed event guards");
}

static void timing_pause_restart_toggle(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[GARDEN_DRAW_MAX];
    begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=1000,.max_hp=1000};
    GardenHitEvent e=hit(8U,0U,0U,1U);
    events(&g,&e,1U); observe_all(&c,&s,&f,&fx,&g);
    for(unsigned age=0;age<=24U;age++) {
        if (age) advance(&g,&c,1U);
        unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,GARDEN_DRAW_MAX,1U,1U);
        CHECK(c.flash[8].remaining==(age<6U ? 6U-age : 0U),"held flash150ms exact expiry");
        CHECK(c.impacts[0].active==(age<8U),"large hit impact200ms exact expiry");
        CHECK(active_popup(&c)==(age<24U),"damage popup600ms exact expiry");
        CHECK(n && d[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"marker remains visible through feedback expiry");
        CHECK(asset_count(d,n,GARDEN_ASSET_FLASH_BOSS1)==0U && asset_count(d,n,GARDEN_ASSET_BOSS1)==1U,"Boss never swaps entire body toWHITE; local impacts retained");
        g.paused=1U; GardenCombat same=c;
        garden_step(&g,0U); garden_combat_observe(&c,&g);
        CHECK(!memcmp(&same,&c,sizeof(c)),"paused core step freezes every combat state/damage group");
        g.paused=0U;
    }
    e=hit(8U,0U,1U,1001U); events(&g,&e,1U); garden_combat_observe(&c,&g);
    advance(&g,&c,11U); CHECK(c.kill_rings[0].active,"kill ring active until12ticks");
    advance(&g,&c,1U); CHECK(!c.kill_rings[0].active,"kill ring expires at300ms");
    e=hit(8U,0U,0U,1U); events(&g,&e,1U); garden_combat_observe(&c,&g);
    garden_combat_set_enabled(&c,0U);
    CHECK(!c.impacts[0].active && !active_popup(&c) && !c.flash[8].remaining,"H presentation disable clears all temporary states");
    events(&g,&e,1U); garden_combat_observe(&c,&g);
    unsigned observed=c.stats.events_observed;
    garden_combat_set_enabled(&c,1U); garden_combat_observe(&c,&g);
    CHECK(!c.impacts[0].active && !active_popup(&c) && c.stats.events_observed==observed,"re-enable cannot replay disabled-time awards");
    events(&g,&e,1U); garden_combat_observe(&c,&g);
    garden_step(&g,GARDEN_INPUT_RESTART); garden_combat_observe(&c,&g);
    CHECK(!c.impacts[0].active && !active_popup(&c) && !c.flash[8].remaining,"actual restart clears held event visuals");
    events(&g,&e,1U); garden_combat_observe(&c,&g);
    g.mode=GARDEN_MODE_BOSS; c.last_mode=GARDEN_MODE_BOSS; g.mode=GARDEN_MODE_WAVES;
    g.combat.count=0U;
    garden_combat_observe(&c,&g);
    CHECK(!c.impacts[0].active && !active_popup(&c),"return-to-WAVES epoch clears even at identical tick");
    puts("PASS: exact 6/8/12/24tick lifetimes, pause freeze, H consumes without replay, restart and epoch clear");
}

static void groups_and_fixed_bounds(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    begin(&g,&c,&s,&f,&fx);
    GardenHitEvent many[24];
    for(unsigned i=0;i<24U;i++) { many[i]=hit(8U,0U,0U,1U);many[i].damage=(uint16_t)(i+1U); }
    events(&g,many,24U);garden_combat_observe(&c,&g);
    CHECK(active_popup(&c)==24U && popup_sum(&c)==300U && c.stats.popups_spawned==24U,
          "every same-step same-target hit gets an independent exact damage label");
    GardenCombat first=c;garden_combat_observe(&c,&g);
    CHECK(!memcmp(&first,&c,sizeof(c)),"duplicate observation never births another24 labels");
    for(unsigned i=0;i<24U;i++) many[i].damage=(uint16_t)(100U+i);
    events(&g,many,24U);saved=g;garden_combat_observe(&c,&g);
    CHECK(active_popup(&c)==32U && c.stats.popups_spawned==48U && c.stats.popup_peak_active==32U &&
          c.stats.popup_replaced==16U && c.stats.popup_pool_exhausted==16U,
          "32-slot cap replaces precisely16 oldest labels during48-hit pressure");
    unsigned old=0U,recent=0U;
    for(unsigned i=0;i<32U;i++) {
        const GardenCombatPopup *p=&c.popups[i];
        if(p->damage<100U) { CHECK(p->damage>=17U && p->damage<=24U,"old same-age birth order retains newest8 of first24");old++; }
        else { CHECK(p->damage<=123U && !p->age,"all second-step labels retain independent zero age");recent++; }
    }
    CHECK(old==8U && recent==24U && c.stats.damage_observed==2976U && c.stats.score_observed==48U,
          "pool replacement does not erase any actual damage or score ledger item");
    CHECK(!c.stats.popups_merged && !c.stats.popups_updated && !c.stats.popup_groups_replaced,
          "legacy grouping/updating counters stayzero for all per-hit pressure");
    CHECK(!memcmp(&saved,&g,sizeof(g)),"pool pressure leaves actual core byte-identical");
    unsigned rejected=c.stats.event_rejected;
    events(&g,many,255U);saved=g;garden_combat_observe(&c,&g);
    CHECK(c.stats.event_rejected==rejected+231U,"malformed count255 remains bounded at24 source events");
    CHECK(active_popup(&c)<=32U && !memcmp(&saved,&g,sizeof(g)),"malformed count cannot grow pools or mutate core");
    begin(&g,&c,&s,&f,&fx);c.stats.popups_spawned=UINT32_MAX-16U;
    for(unsigned i=0;i<24U;i++) { many[i]=hit(8U,0U,0U,1U);many[i].damage=(uint16_t)(i+1U); }
    events(&g,many,24U);garden_combat_observe(&c,&g);
    for(unsigned i=0;i<24U;i++) many[i].damage=(uint16_t)(100U+i);
    events(&g,many,24U);garden_combat_observe(&c,&g);
    old=0U;
    for(unsigned i=0;i<32U;i++) if(c.popups[i].damage<100U) {
        CHECK(c.popups[i].damage>=17U && c.popups[i].damage<=24U,
              "oldest same-age selection retains correct birth order acrossUINT32 sequence wrap");old++;
    }
    CHECK(old==8U && c.stats.popup_replaced==16U,"sequence wrap does not change fixed32 oldest replacement count");
    CHECK(sizeof(GardenCombat)<1600U,"fixed combat state stays below1600 bytes, without dynamic allocation");
    puts("PASS: independent same-step hits,32 pool oldest/birth-order replacements, separate ledgers, reserved-zero legacy counters and count255 bound");
}

static void full_pool_and_draw_bounds(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    begin(&g,&c,&s,&f,&fx);
    g.demo_mode=1U; g.player.grace=0U;
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=1000,.max_hp=1000,.recycle=3,.phase_ticks=200};
    for(unsigned i=0;i<8U;i++) g.enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=100000+(int32_t)i*30000,.y=180000,.hp=200};
    for(unsigned i=0;i<24U;i++) g.player_bullets[i]=(GardenPlayerBullet){.active=1,.x=100000+(int32_t)i*10000,.y=300000,.damage=95};
    for(unsigned i=0;i<192U;i++) g.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=1,.x=200000,.y=250000};
    for(unsigned i=0;i<8U;i++) g.effects[i]=(GardenEffect){.active=1,.x=250000,.y=220000,.big=1,.age=0};
    GardenHitEvent many[3]={hit(0U,GARDEN_ENEMY_DARTER,1U,21U),hit(1U,GARDEN_ENEMY_STOPPER,1U,81U),hit(8U,0U,0U,1U)};
    events(&g,many,3U); observe_all(&c,&s,&f,&fx,&g); saved=g;
    struct Guard { uint64_t before; GardenDraw draws[240]; uint64_t after; } guard;
    for(unsigned alpha=0;alpha<=1U;alpha++) for(unsigned cap=0;cap<=250U;cap++) {
        memset(&guard,0xA5,sizeof(guard));
        uint64_t before=guard.before,after=guard.after;
        unsigned limit=cap<240U ? cap : 240U;
        GardenDraw untouched[240]; memcpy(untouched,guard.draws,sizeof(untouched));
        unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,guard.draws,cap,alpha,0U);
        check_score_layout(&g,guard.draws,n);
        CHECK(n<=limit && guard.before==before && guard.after==after,"all capacities0..250 protect allocation canaries");
        for(unsigned i=limit;i<240U;i++) CHECK(!memcmp(&guard.draws[i],&untouched[i],sizeof(GardenDraw)),"no write beyond caller capacity");
        CHECK(!memcmp(&saved,&g,sizeof(g)),"every constrained draw build is game-pure");
        if(cap>=234U) CHECK(c.stats.essential_dropped==0U,"all234 necessary actors/projectiles/effects preserved before any decor");
        if(cap>=235U) CHECK(c.stats.marker==1U && guard.draws[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"full pool still prioritizes exact marker");
        for(unsigned i=0;i<n;i++) {
            CHECK(guard.draws[i].asset<GARDEN_ASSET_COUNT || guard.draws[i].asset>=GARDEN_DRAW_SCORE_BASE,"asset is real or supported composite");
            CHECK(guard.draws[i].alpha<=32U,"every opacity bounded");
            if(!alpha) CHECK(guard.draws[i].asset>=GARDEN_DRAW_SCORE_BASE ? guard.draws[i].alpha==32U : 1,"Alpha-off damage composite opaque");
        }
        if(cap>=240U) {
            CHECK(n==240U && c.stats.popups>=2U && c.stats.kill_rings==2U,"five available full-pool slots admit both kill groups/shocks before low-priority decor");
            CHECK(asset_count(guard.draws,n,GARDEN_ASSET_SHOT)==24U,"no player shot is discarded for scoring effects");
        }
    }
    CHECK(garden_combat_build(&c,&s,&f,&fx,&g,0,240U,1U,1U)==0U,"null draw buffer is safe");
    GardenDraw out[240]; g.player.respawning=5U;
    garden_combat_build(&c,&s,&f,&fx,&g,out,240U,1U,1U);
    CHECK(!c.stats.marker,"respawning player has no misleading marker");
    g.player.respawning=0U; g.player.grace=1U;
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,out,240U,1U,1U);
    CHECK(out[n-1U].asset==GARDEN_ASSET_FOCUS_GRACE,"invulnerable marker distinct from vulnerable marker");
    garden_fx_set_flags(&fx,GARDEN_FX_TRAIL|GARDEN_FX_RING);
    garden_combat_build(&c,&s,&f,&fx,&g,out,240U,1U,1U);
    CHECK(c.stats.held_flashes==0U,"held white respects FLASH flag");
    puts("PASS: capacities0..250 and null canaries, full234 priority, no core mutation, opacity/asset bounds and marker last");
}

static void popup_geometry_and_layer_order(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240];
    for(unsigned corner=0;corner<4U;corner++) {
        begin(&g,&c,&s,&f,&fx);
        GardenHitEvent e=hit(8U,0U,1U,1001U);
        e.x=e.target_x=(corner&1U) ? INT32_MAX : INT32_MIN;
        e.y=e.target_y=(corner&2U) ? INT32_MAX : INT32_MIN;
        events(&g,&e,1U); observe_all(&c,&s,&f,&fx,&g);
        for(unsigned age=0;age<24U;age++) {
            if(age) advance(&g,&c,1U);
            unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
            for(unsigned i=0;i<n;i++) if(d[i].asset>=GARDEN_DRAW_SCORE_BASE) {
                CHECK(d[i].asset==GARDEN_DRAW_SCORE_BASE+95U,"large-coordinate popup keeps exact effective damage");
                CHECK(d[i].x>=0 && d[i].x+44<=640 && d[i].y>=0 && d[i].y+16<=480,"full16px damage bbox stays withinfield at all corners/rise ages");
            }
        }
    }
    begin(&g,&c,&s,&f,&fx);
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=320000,.y=200000,.damage=95};
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=1,.x=300000,.y=200000};
    GardenHitEvent e=hit(8U,0U,0U,1U); events(&g,&e,1U); observe_all(&c,&s,&f,&fx,&g);
    f.sparks[0]=(GardenHitSpark){321,146,1,0};
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(f.sparks[0].active,"former observer history restored after duplicate-spark filtering");
    CHECK(asset_count(d,n,GARDEN_ASSET_HIT_SPARK)==1U,"old13px duplicate suppressed when strong explicit feedback is enabled");
    unsigned shot=0U,ring=0U,popup=0U;
    for(unsigned i=0;i<n;i++) {
        if(d[i].asset==GARDEN_ASSET_SHOT) shot=i;
        if(d[i].asset==GARDEN_ASSET_FX_RING32) ring=i;
        if(d[i].asset>=GARDEN_DRAW_SCORE_BASE) popup=i;
    }
    CHECK(ring>shot && popup>shot && d[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"strong hit and score paint after projectiles, collision centre last");
    puts("PASS: full composite bbox corner/rise clipping, duplicate filtering restores observer, explicit painter order");
}

static void overlapped_slots_and_reuse(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240]; begin(&g,&c,&s,&f,&fx);
    for (unsigned i=0;i<2U;i++) g.enemies[i]=(GardenEnemy){.active=1,
        .type=GARDEN_ENEMY_DARTER,.x=200000,.y=150000,.hp=200};
    GardenHitEvent e=hit(1U,GARDEN_ENEMY_DARTER,0U,1U);
    e.target_x=200000; e.target_y=150000; e.x=201000; e.y=154000;
    events(&g,&e,1U); observe_all(&c,&s,&f,&fx,&g);
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U),found=0U;
    for (unsigned i=0;i<n;i++) if(d[i].x==200 && d[i].y==150 &&
        (d[i].asset==GARDEN_ASSET_DARTER1 || d[i].asset==GARDEN_ASSET_FLASH_DARTER1)) {
        CHECK(d[i].asset==(found ? GARDEN_ASSET_FLASH_DARTER1 : GARDEN_ASSET_DARTER1),
              "overlapping identical enemy bodies retain actual slot-corresponding flash");
        found++;
    }
    CHECK(found==2U && c.stats.held_flashes==1U,"exactly the hit target flashes under overlap");
    g.enemies[1].type=GARDEN_ENEMY_STOPPER;
    garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(!c.stats.held_flashes,"reused slot with changed enemy type cannot inherit old flash");
    g.enemies[1].type=GARDEN_ENEMY_DARTER; g.enemies[1].x+=65000;
    garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(!c.stats.held_flashes,"teleported/reused same-type slot cannot inherit distant old flash");
    puts("PASS: held flashes key actual overlapping enemy slot, type and teleport reuse guards");
}

static void actual_independent_boss_hits_and_edge_layout(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240],repeat[240];
    begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=100000,.max_hp=100000,
        .recycle=500,.phase_ticks=500};
    for(unsigned step=0;step<13U;step++) {
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=95};
        garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
    }
    CHECK(g.score==13U && c.stats.events_observed==13U && active_popup(&c)==13U,
          "actual thirteen Boss hits retain thirteen independent labels");
    CHECK(popup_sum(&c)==1235U && c.stats.damage_observed==1235U && !c.stats.popup_groups_replaced,"thirteen labels retain each95 damage and all1235 effective HP audit");
    garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    GardenCombatPopup history[GARDEN_COMBAT_POPUP_MAX]; memcpy(history,c.popups,sizeof(history));
    for(unsigned cap=0;cap<=250U;cap++) {
        unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,cap,1U,1U);
        check_score_layout(&g,d,n);
        CHECK(!memcmp(history,c.popups,sizeof(history)),"admission-only layout leaves actual group value/position/age untouched");
        if(cap>=240U) CHECK(c.stats.popups==13U,"all thirteen actual hit labels paint with normal full draw budget");
        if(c.stats.marker) CHECK(d[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"spacing does not move marker from last painter slot");
    }
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    g.paused=1U; garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
    unsigned again=garden_combat_build(&c,&s,&f,&fx,&g,repeat,240U,1U,1U);
    CHECK(n==again,"pause preserves damage layout count");
    for(unsigned i=0;i<n;i++) CHECK(d[i].x==repeat[i].x && d[i].y==repeat[i].y &&
        d[i].asset==repeat[i].asset && d[i].alpha==repeat[i].alpha,"same-tick and pause preserve exact painter placement");
    g.paused=0U;
    /* Three clamped groups at both field axes, at every floating age. This
     * includes the widest supported composite, independent of current rules.
     */
    for(unsigned corner=0;corner<4U;corner++) for(unsigned age=0;age<24U;age++) {
        begin(&g,&c,&s,&f,&fx);
        for(unsigned i=0;i<3U;i++) c.popups[i]=(GardenCombatPopup){
            (int16_t)((corner&1U) ? 639 : 0),(int16_t)((corner&2U) ? 471 : 8),
            (uint16_t)(i==0U ? 1U : i==1U ? 1001U : 32767U),1U,(uint8_t)age,(uint8_t)i,(uint8_t)(i==2U),0U,0U,0U,0,0,0U,0U,0U,0U,0U};
        for(unsigned i=0;i<3U;i++) {
            c.popups[i].target_type=GARDEN_ENEMY_DARTER;
            c.popups[i].target_x=(corner&1U) ? 639000:0;
            c.popups[i].target_y=(corner&2U) ? 479000:0;
            c.popups[i].spawn_sequence=i;
        }
        garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
        memcpy(history,c.popups,sizeof(history));
        n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
        CHECK(c.stats.popups==3U,"all edge groups admitted including kill-priority and different widths");
        check_score_layout(&g,d,n);
        CHECK(!memcmp(history,c.popups,sizeof(history)),"edge placement cannot alter groups or lifetime");
        CHECK(d[n-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"edge popup placement leaves marker last");
    }
    begin(&g,&c,&s,&f,&fx);
    for(unsigned i=0;i<3U;i++) c.popups[i]=(GardenCombatPopup){(int16_t)(50+i*200),100,1U,1U,0U,(uint8_t)i,0U,0U,0U,0U,0,0,0U,0U,0U,0U,0U};
    n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(c.stats.popups==3U,"manual distinct targets retain three anchored labels");
    check_score_layout(&g,d,n);
    puts("PASS: actual thirteen independent Boss hit labels, full draw admission, field edges/rise and same-tick/pause");
}

static void effective_damage_not_reward_or_overkill(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240];
    begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=142,.max_hp=142,
        .recycle=100,.phase_ticks=100};
    for(unsigned i=0;i<2U;i++) {
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=95};
        garden_step(&g,0U); saved=g; observe_all(&c,&s,&f,&fx,&g);
        CHECK(g.combat.count==1U && g.combat.items[0].damage==(i ? 47U : 95U),
              "production core records effective95 then47; overkill48 is absent");
        CHECK(!memcmp(&saved,&g,sizeof(g)),"damage observer leaves entire actual game untouched");
    }
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(g.score==1002U && c.stats.score_observed==1002U && c.stats.damage_observed==142U,
          "real+1 scores and1000 kill reward retained separately from142 total HP damage");
    CHECK(active_popup(&c)==2U && popup_sum(&c)==142U &&
          asset_count(d,n,GARDEN_DRAW_SCORE_BASE+47U)==1U &&
          asset_count(d,n,GARDEN_DRAW_SCORE_BASE+95U)==1U,
          "effective95 and final47 both paint independent labels, without reward or overkill");
    check_score_layout(&g,d,n);
    for(unsigned type=GARDEN_ENEMY_DARTER;type<=GARDEN_ENEMY_STOPPER;type++) {
        begin(&g,&c,&s,&f,&fx);
        g.enemies[0]=(GardenEnemy){.active=1,.type=(uint8_t)type,.phase=2,.recycle=100,
            .target_time=100,.x=200000,.y=150000,.hp=37};
        int32_t iy=type==GARDEN_ENEMY_DARTER ? 154000 : 120000;
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=201000,.y=iy+14000,.vy=-14000,.damage=95};
        garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
        CHECK(g.combat.count==1U && g.combat.items[0].damage==37U && popup_sum(&c)==37U,
              "actual enemy killing95 projectile only shows remaining37HP");
        CHECK(g.score==(type==GARDEN_ENEMY_DARTER ? 21U : 81U),
              "enemy overkill never alters original rewards plus hit point");
    }
    begin(&g,&c,&s,&f,&fx);
    g.score=UINT32_MAX;
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=47,.max_hp=47,
        .recycle=100,.phase_ticks=100};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=95};
    garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
    CHECK(g.score==UINT32_MAX && g.combat.items[0].score_delta==0U &&
          c.stats.score_observed==0U && c.stats.damage_observed==47U && popup_sum(&c)==47U,
          "actual saturated score+0 still displays effective-47 killing damage");
    begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=1000,.max_hp=1000,
        .recycle=100,.phase_ticks=100};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=0};
    garden_step(&g,0U); observe_all(&c,&s,&f,&fx,&g);
    CHECK(!g.combat.count && !c.stats.damage_observed && !active_popup(&c),
          "actual zero-damage contact produces no fabricated negative damage");
    puts("PASS: actual effective HP damage, enemy/Boss overkill, independent final killing damage, score saturation and zero damage");
}

static void presentation_damage_value_saturation(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    begin(&g,&c,&s,&f,&fx);
    GardenHitEvent e=hit(8U,0U,0U,1U);e.damage=UINT16_MAX;
    for(unsigned i=0;i<2U;i++) { events(&g,&e,1U);saved=g;garden_combat_observe(&c,&g); }
    CHECK(active_popup(&c)==2U && popup_sum(&c)==65534U && c.stats.popups_spawned==2U &&
          c.stats.damage_observed==131070U && c.stats.damage_display_clamped==65536U,
          "each independent15-bit label counts its own32768 omitted damage units without grouping");
    CHECK(c.stats.score_observed==2U && !memcmp(&saved,&g,sizeof(g)),
          "label clipping never changes core or score ledger");
    GardenCombat same=c;garden_combat_observe(&c,&g);
    CHECK(!memcmp(&same,&c,sizeof(c)),"same-tick clipping cannot count twice");
    CHECK(!c.stats.popups_merged && !c.stats.popups_updated && !c.stats.popup_groups_replaced,
          "large values still never group or update an old label");
    puts("PASS: independent32767 label caps, full observed HP ledger, exact omitted-unit accounting and no wrapping/replay");
}

static void health_bar_avoidance_at_edges(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240];
    for(unsigned slot=0;slot<GARDEN_MAX_ENEMIES+1U;slot++)
    for(unsigned corner=0;corner<4U;corner++) {
        begin(&g,&c,&s,&f,&fx);
        int32_t x=(corner&1U) ? 639000 : 0;
        int32_t y=(corner&2U) ? 479000 : 0;
        unsigned type=slot==GARDEN_MAX_ENEMIES ? 0U :
            slot&1U ? GARDEN_ENEMY_STOPPER : GARDEN_ENEMY_DARTER;
        if(slot==GARDEN_MAX_ENEMIES) g.boss=(GardenBoss){.active=1,.x=x,.y=y,.hp=1000,.max_hp=1000};
        else g.enemies[slot]=(GardenEnemy){.active=1,.type=(uint8_t)type,.x=x,.y=y,.hp=100};
        GardenHitEvent e=hit(slot,type,0U,1U);
        e.x=e.target_x=x; e.y=e.target_y=y;
        events(&g,&e,1U); observe_all(&c,&s,&f,&fx,&g);
        for(unsigned age=0;age<24U;age++) {
            if(age) advance(&g,&c,1U);
            unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
            CHECK(c.stats.popups==1U,"edge damage label remains admitted through lifetime");
            check_score_layout(&g,d,n);
        }
    }
    begin(&g,&c,&s,&f,&fx);
    for(unsigned slot=0;slot<8U;slot++) g.enemies[slot]=(GardenEnemy){
        .active=1,.type=(uint8_t)(slot&1U ? GARDEN_ENEMY_STOPPER : GARDEN_ENEMY_DARTER),
        .x=320000,.y=(int32_t)(slot*30000U+41000U),.hp=100};
    g.boss=(GardenBoss){.active=1,.x=320000,.y=70000,.hp=1000,.max_hp=1000};
    for(unsigned i=0;i<3U;i++) c.popups[i]=(GardenCombatPopup){320,8,
        (uint16_t)(i==0U ? 1U : i==1U ? 95U : 32767U),1U,0U,(uint8_t)i,0U,0U,0U,0U,0,0,0U,0U,0U,0U,0U};
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(c.stats.popups==3U,"three varying-width labels survive all nine overlapping-x live HP bars");
    check_score_layout(&g,d,n);
    puts("PASS: all enemy/Boss HP bar geometry at all field corners and rising ages, plus nine-bar/three-popup overlap");
}

static GardenDraw only_damage_draw(const GardenDraw *draws,unsigned count)
{
    GardenDraw found={0};unsigned n=0U;
    for(unsigned i=0;i<count;i++) if(draws[i].asset>=GARDEN_DRAW_SCORE_BASE) { found=draws[i];n++; }
    CHECK(n==1U,"one actual hit paints precisely one independent HP-loss label");
    return found;
}

static void independent_birth_anchors_pause_and_overlap(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240];begin(&g,&c,&s,&f,&fx);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=100000,.max_hp=100000,
        .recycle=500,.phase_ticks=500};
    GardenHitEvent pair[2]={hit(8U,0U,0U,1U),hit(8U,0U,0U,1U)};
    pair[0].x=316000;pair[1].x=324000; /* Actual dual shot±4, independent of birth sides. */
    events(&g,pair,2U);observe_all(&c,&s,&f,&fx,&g);
    unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(n<=240U,"initial anchor build respects fixed draw budget");
    CHECK(active_popup(&c)==2U && c.stats.popups==2U && popup_sum(&c)==190U,
          "two simultaneous effective hits retain two separately anchored values");
    GardenDraw left,right;CHECK(garden_combat_popup_draw(&c,0U,1U,&left) && garden_combat_popup_draw(&c,1U,1U,&right),"pure per-identity draw readout returns both active labels");
    CHECK(left.x==right.x && left.y==right.y && left.x+22==320 && c.popups[0].rise_budget==30U,"same target dual hits are intentionally centred/overlapping above HP with one-time30px rise budget");
    CHECK(c.stats.popup_anchors_chosen==2U && !c.stats.popup_anchor_failures,"both born anchors choose exactly once");
    GardenCombatPopup old[2]={c.popups[0],c.popups[1]};
    for(unsigned step=1U;step<12U;step++) {
        advance(&g,&c,1U);g.boss.x+=1000;g.boss.y+=500;
        garden_combat_set_render_fraction(&c,128U);
        n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
        CHECK(n<=240U,"moving target build respects fixed draw budget");
        CHECK(c.popups[0].x==old[0].x && c.popups[0].y==old[0].y &&
              c.popups[1].x==old[1].x && c.popups[1].y==old[1].y && c.stats.popup_anchors_chosen==2U,
              "later target/pose motion never changes established birth anchors or chooses again");
        CHECK(c.popups[0].age==step && c.popups[1].age==step,"each label has its own immutable hit-age timeline");
    }
    pair[0].target_x=g.boss.x;pair[0].target_y=g.boss.y;events(&g,pair,1U);garden_combat_observe(&c,&g);
    CHECK(active_popup(&c)==3U && c.popups[0].age==12U && c.popups[2].age==0U,
          "new hit creates new label without refreshing any earlier age");
    n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
    CHECK(n<=240U,"third independent birth respects fixed draw budget");
    CHECK(c.stats.popup_anchors_chosen==3U && !c.stats.popups_updated && !c.stats.popups_merged,
          "each true hit anchors only its own label, with no update/merge path");
    g.paused=1U;GardenCombat frozen=c;garden_step(&g,0U);garden_combat_observe(&c,&g);
    CHECK(!memcmp(&frozen,&c,sizeof(c)),"pause preserves independent ages/anchors and retainedQ8 fraction");
    g.paused=0U;advance(&g,&c,12U);
    CHECK(active_popup(&c)==1U && c.popups[2].age==12U,"older labels expire600ms after their own births, later hit continues independently");
    advance(&g,&c,12U);CHECK(!active_popup(&c),"latest label has its own exact600ms deadline");
    garden_combat_set_render_fraction(&c,UINT32_MAX);CHECK(c.render_fraction_q8==255U,"fraction clamp remains bounded");
    garden_step(&g,GARDEN_INPUT_RESTART);garden_combat_observe(&c,&g);
    CHECK(!active_popup(&c),"restart clears every established birth path");
    CHECK(!garden_combat_popup_draw(0,0U,1U,&left) && !garden_combat_popup_draw(&c,32U,1U,&left) && !garden_combat_popup_draw(&c,0U,1U,0),"pure diagnostics safely reject null/out-of-range/inactive inputs");
    puts("PASS: independent overlapping centred head anchors, fixed rise budgets, no relayout under target movement, independent ages/no refreshing, pause and restart");
}

static void model_shell_and_reused_slot_layout(void)
{
    GardenGame g; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240];
    for(unsigned top=0;top<3U;top++) {
        begin(&g,&c,&s,&f,&fx);
        g.boss=(GardenBoss){.active=1,.x=320000,.y=top==0U ? 117000:top==1U ? 50000:0,
            .hp=50000,.max_hp=50000,.charge_ticks=20};
        s.enabled=1U;s.charge_countdown=1U;s.low_hp=1U;s.burst_ticks=1U;s.last_tick=8U;
        GardenHitEvent e=hit(8U,0U,0U,1U);e.target_y=g.boss.y;e.y=g.boss.y+20000;
        events(&g,&e,1U);garden_combat_observe(&c,&g);
        unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,1U,1U);
        GardenDraw label=only_damage_draw(d,n);
        unsigned width=score_width(label.asset-GARDEN_DRAW_SCORE_BASE);
        CHECK(label.x+(int)width/2==320,"head popup centres on the true target, never either side");
        check_score_layout(&g,d,n);
        if(!top) CHECK(label.y==29 && c.popups[0].rise_budget==27U,"117px Boss gives29px head birth and bounded27px rise to2");
        else CHECK(label.y==18 && c.popups[0].rise_budget==8U,"top-entry head fallback births18 below HP and rises8 to10, never crosses HP");
    }
    begin(&g,&c,&s,&f,&fx);
    GardenHitEvent e=hit(0U,GARDEN_ENEMY_DARTER,1U,21U);
    events(&g,&e,1U);garden_combat_observe(&c,&g);
    CHECK(active_popup(&c)==1U,"post-kill lane is visible during short death feedback");
    g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=320000,.y=120000,.hp=300};
    advance(&g,&c,1U);
    CHECK(!active_popup(&c) && c.stats.popup_stale_cleared==1U,"slot newly live after killing hit clears old actor's independent damage label");
    e=hit(0U,GARDEN_ENEMY_DARTER,0U,1U);events(&g,&e,1U);garden_combat_observe(&c,&g);
    g.enemies[0].type=GARDEN_ENEMY_STOPPER;advance(&g,&c,1U);
    CHECK(!active_popup(&c),"reused slot type change clears old damage lane without a new hit");
    e=hit(0U,GARDEN_ENEMY_STOPPER,0U,1U);events(&g,&e,1U);garden_combat_observe(&c,&g);
    g.enemies[0].x+=65000;advance(&g,&c,1U);
    CHECK(!active_popup(&c),"teleported/reused slot cannot inherit a distant damage lane");
    puts("PASS: centred Boss head policy, bounded-rise/entry below HP, death coordinates and killed/type/teleport slot reuse");
}

static void head_budget_and_target_hp_clearance(void)
{
    GardenGame g;GardenCombat c;GardenScene s;GardenFeedback f;GardenFx fx;GardenDraw draws[240];
    const int target_y[]={-80,0,40,90,91,117,121,122,479};
    const unsigned budgets[]={8U,8U,8U,0U,1U,27U,31U,32U,32U};
    for(unsigned i=0;i<sizeof(target_y)/sizeof(target_y[0]);i++) {
        begin(&g,&c,&s,&f,&fx);
        g.boss=(GardenBoss){.active=1,.x=320000,.y=target_y[i]*1000,.hp=1000,.max_hp=1000};
        GardenHitEvent e=hit(8U,0U,0U,1U);e.target_y=g.boss.y;e.y=g.boss.y+20000;
        events(&g,&e,1U);observe_all(&c,&s,&f,&fx,&g);
        garden_combat_build(&c,&s,&f,&fx,&g,draws,240U,1U,1U);
        GardenCombatPopup before=c.popups[0];
        CHECK(before.rise_budget==budgets[i] && before.x==320,"each boundary gets its exact fixed rise budget and target-centred anchor");
        int bar_top=target_y[i]-70;if(bar_top<0) bar_top=0;if(bar_top>472) bar_top=472;
        for(unsigned age=0;age<24U;age++) {
            if(age) advance(&g,&c,1U);
            for(unsigned fraction=0;fraction<=255U;fraction+=51U) {
                garden_combat_set_render_fraction(&c,fraction);GardenDraw d;
                CHECK(garden_combat_popup_draw(&c,0U,1U,&d),"head identity remains readable through independent lifetime");
                CHECK(d.x==before.x-22 && d.y>=2 && d.y+16<=478 &&
                      (d.y+18<=bar_top || d.y>=bar_top+10),"entire bounded head/entry path avoids its own HP bar");
                CHECK(c.popups[0].x==before.x && c.popups[0].y==before.y && c.popups[0].rise_budget==before.rise_budget,
                      "head path never clamps, changes side or recomputes budget during life");
            }
        }
    }
    puts("PASS: exact0/1/27/31/32 rise budgets, top-entry8 fallback, centred targets and own HP clearance through complete Q8 path");
}

static void long_core_observer_purity(void)
{
    GardenGame g,saved; GardenCombat c; GardenScene s; GardenFeedback f; GardenFx fx;
    GardenDraw d[240]; garden_init(&g,12345U); garden_combat_reset(&c);
    garden_scene_reset(&s); garden_feedback_reset(&f); garden_fx_reset(&fx);
    for(unsigned i=0;i<12000U;i++) {
        uint16_t input=0U;
        if(i==100U || i==120U || i==8100U || i==8120U) input=GARDEN_INPUT_PAUSE;
        if(i==9000U) input=GARDEN_INPUT_RESTART;
        garden_step(&g,input); saved=g; observe_all(&c,&s,&f,&fx,&g);
        unsigned n=garden_combat_build(&c,&s,&f,&fx,&g,d,240U,i&1U,1U);
        CHECK(n<=240U && !memcmp(&saved,&g,sizeof(g)),"12000 production steps preserve every game byte through observer/build");
        for(unsigned j=0;j<n;j++) CHECK(d[j].alpha<=32U,"long production run bounded opacity");
    }
    CHECK(c.stats.events_observed>0U && c.stats.kills_observed>0U && c.stats.score_observed>0U && c.stats.damage_observed>0U,"real demo exercised hits/kills/awards and effective HP damage");
    CHECK(!c.stats.event_rejected && !c.stats.event_gaps && !c.stats.essential_drop_total,"actual every-step demo events retained with no malformed/gap/essentialdrop");
    CHECK(c.stats.popups_spawned==c.stats.events_observed && !c.stats.popup_pool_exhausted &&
          !c.stats.popups_merged && !c.stats.popups_updated && !c.stats.popup_groups_replaced,
          "12000 real production steps birth every observed hit independently without exhaustion or legacy grouping");
    CHECK(!c.stats.popup_anchor_failures && c.stats.popup_anchors_chosen==c.stats.popups_spawned,
          "12000 natural production steps choose exactly one safe swept anchor for every independent real hit");
    printf("INFO: natural_popup_births=%u natural_anchors=%u natural_peak=%u natural_stale_cleared=%u\n",
           c.stats.popups_spawned,c.stats.popup_anchors_chosen,c.stats.popup_peak_active,c.stats.popup_stale_cleared);
    puts("PASS:12000 actual production-step complete-state snapshots including pause/restart and real events");
}

int main(void)
{
    actual_events(); freshness_credit_and_invalid(); timing_pause_restart_toggle();
    groups_and_fixed_bounds(); full_pool_and_draw_bounds(); popup_geometry_and_layer_order();
    overlapped_slots_and_reuse();
    actual_independent_boss_hits_and_edge_layout();
    effective_damage_not_reward_or_overkill();
    presentation_damage_value_saturation();
    health_bar_avoidance_at_edges();
    independent_birth_anchors_pause_and_overlap();
    model_shell_and_reused_slot_layout();
    head_budget_and_target_hp_clearance();
    long_core_observer_purity();
    printf("INFO: combat_state_bytes=%u popup_bytes=%u popup_pool_slots=%u rise_pixels=%u jitter_pixels=%u\n",
           (unsigned)sizeof(GardenCombat),(unsigned)sizeof(GardenCombatPopup),
           GARDEN_COMBAT_POPUP_MAX,GARDEN_COMBAT_POPUP_RISE,GARDEN_COMBAT_POPUP_JITTER);
    printf("PASS: combat feedback host contract checks=%u\n",checks);
    return 0;
}
