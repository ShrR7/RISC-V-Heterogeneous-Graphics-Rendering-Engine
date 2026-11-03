/* SPDX-License-Identifier: GPL-3.0-or-later
 * Fixed event-driven presentation. Never writes the game or the GPU.
 */
#include "garden_combat.h"
#include "garden_assets.h"
#include "garden_actor_ui.h"
#include <string.h>

static void clear_temporary(GardenCombat *c)
{
    memset(c->flash,0,sizeof(c->flash));
    memset(c->impacts,0,sizeof(c->impacts));
    memset(c->kill_rings,0,sizeof(c->kill_rings));
    memset(c->popups,0,sizeof(c->popups));
}

void garden_combat_reset(GardenCombat *c)
{
    if (!c) return;
    memset(c,0,sizeof(*c));
    c->enabled=1U;
}

void garden_combat_set_render_fraction(GardenCombat *c,unsigned fraction_q8)
{ if (c) c->render_fraction_q8=fraction_q8>255U ? 255U : fraction_q8; }

void garden_combat_set_enabled(GardenCombat *c,unsigned enabled)
{
    if (!c) return;
    enabled=enabled!=0U;
    if (c->enabled!=enabled) clear_temporary(c);
    c->enabled=enabled;
}

static int16_t clamp_pixel(int32_t raw,unsigned border,unsigned maximum)
{
    int32_t p=raw/GARDEN_GRAIN;
    if (p<(int32_t)border) p=(int32_t)border;
    if (p>(int32_t)(maximum-border-1U)) p=(int32_t)(maximum-border-1U);
    return (int16_t)p;
}

static void age_impacts(GardenCombatImpact *pool,unsigned count,
                        unsigned lifetime,uint32_t elapsed)
{
    for (unsigned i=0;i<count;i++) if (pool[i].active) {
        if (elapsed>=lifetime-pool[i].age) pool[i].active=0U;
        else pool[i].age=(uint8_t)(pool[i].age+elapsed);
    }
}

static void age_temporary(GardenCombat *c,uint32_t elapsed)
{
    age_impacts(c->impacts,GARDEN_COMBAT_IMPACT_MAX,GARDEN_COMBAT_IMPACT_TICKS,elapsed);
    age_impacts(c->kill_rings,GARDEN_COMBAT_KILL_MAX,GARDEN_COMBAT_KILL_TICKS,elapsed);
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES+1U;i++) {
        unsigned remaining=c->flash[i].remaining;
        c->flash[i].remaining=(uint8_t)(elapsed>=remaining ? 0U : remaining-elapsed);
    }
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) if (c->popups[i].active) {
        GardenCombatPopup *p=&c->popups[i];
        if (elapsed>=GARDEN_COMBAT_POPUP_TICKS-p->age) { p->active=0U; continue; }
        p->age=(uint8_t)(p->age+elapsed);
        p->motion_age=p->age;
    }
}

static void spawn_impact(GardenCombatImpact *pool,unsigned count,
                         int32_t x,int32_t y,unsigned border,
                         uint32_t *spawned,uint32_t *replaced)
{
    if (pool[count-1U].active) (*replaced)++;
    for (unsigned i=count-1U;i>0U;i--) pool[i]=pool[i-1U];
    pool[0]=(GardenCombatImpact){clamp_pixel(x,border,GARDEN_WIDTH),
                                clamp_pixel(y,border,GARDEN_HEIGHT),1U,0U};
    (*spawned)++;
}

static unsigned spawn_popup(GardenCombat *c,const GardenHitEvent *e)
{
    if (!e->damage) return GARDEN_COMBAT_POPUP_MAX; /* Positive effective HP only. */
    unsigned damage=e->damage;
    unsigned chosen=GARDEN_COMBAT_POPUP_MAX;
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)
        if (!c->popups[i].active) { chosen=i; break; }
    if (chosen==GARDEN_COMBAT_POPUP_MAX) {
        /* Each tick/target group gets an independent label. Pool pressure replaces
         * the oldest presentation, not a core event or HP/score ledger item.
         * Same-tick ties use birth order, including counter wrap safely. */
        chosen=0U;
        for (unsigned i=1;i<GARDEN_COMBAT_POPUP_MAX;i++) {
            const GardenCombatPopup *p=&c->popups[i],*old=&c->popups[chosen];
            uint32_t age_order=c->stats.popups_spawned-p->spawn_sequence;
            uint32_t old_order=c->stats.popups_spawned-old->spawn_sequence;
            if (p->age>old->age || (p->age==old->age && age_order>old_order)) chosen=i;
        }
        c->stats.popup_pool_exhausted++;c->stats.popup_replaced++;
    }
    if (damage>32767U) {
        c->stats.damage_display_clamped+=damage-32767U;
        damage=32767U;
    }
    /* Only record the actual event here. The first presentation build has
     * the shared scene pose and chooses one centred, bounded-rise head anchor.
     * Future frames retain it, independent of bullets/target movement. */
    c->popups[chosen]=(GardenCombatPopup){.x=clamp_pixel(e->target_x,0U,GARDEN_WIDTH),
        .y=clamp_pixel(e->target_y,0U,GARDEN_HEIGHT),.damage=(uint16_t)damage,
        .active=1U,.slot=e->target_slot,.killed=(uint8_t)(e->killed!=0U),
        .target_type=e->target_type,.target_x=e->target_x,.target_y=e->target_y,
        .spawn_sequence=c->stats.popups_spawned};
    c->stats.popups_spawned++;
    unsigned active=0U;
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) active+=c->popups[i].active!=0U;
    if (active>c->stats.popup_peak_active) c->stats.popup_peak_active=active;
    return chosen;
}

static unsigned valid_event(const GardenHitEvent *e)
{
    if (!e->damage || e->target_slot>GARDEN_MAX_ENEMIES || e->boss>1U ||
        e->killed>1U || (e->boss!=(e->target_slot==GARDEN_MAX_ENEMIES))) return 0U;
    if ((e->boss && e->target_type!=GARDEN_ENEMY_NONE) ||
        (!e->boss && e->target_type!=GARDEN_ENEMY_DARTER &&
         e->target_type!=GARDEN_ENEMY_STOPPER)) return 0U;
    uint32_t reward=e->boss ? 1000U : e->target_type==GARDEN_ENEMY_DARTER ? 20U : 80U;
    if (e->kill_reward!=(e->killed ? reward : 0U) || e->score_delta>32767U ||
        (uint64_t)e->score_delta>1U+(uint64_t)e->kill_reward) return 0U;
    return 1U;
}

static void clear_reused_target_lanes(GardenCombat *c,const GardenGame *g)
{
    for (unsigned slot=0;slot<GARDEN_MAX_ENEMIES+1U;slot++) {
        unsigned active,type;int32_t x,y;
        if (slot==GARDEN_MAX_ENEMIES) {
            active=g->boss.active;type=GARDEN_ENEMY_NONE;x=g->boss.x;y=g->boss.y;
        } else {
            const GardenEnemy *e=&g->enemies[slot];
            active=e->active;type=e->type;x=e->x;y=e->y;
        }
        unsigned new_actor=active && c->initialized && !c->target_was_active[slot];
        for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) {
            GardenCombatPopup *p=&c->popups[i];
            if (!p->active || p->slot!=slot || !active) continue;
            int64_t dx=(int64_t)x-p->target_x,dy=(int64_t)y-p->target_y;
            if (new_actor || p->killed || type!=p->target_type ||
                dx<-64000 || dx>64000 || dy<-64000 || dy>64000) {
                p->active=0U;c->stats.popup_stale_cleared++;
            }
        }
        c->target_was_active[slot]=(uint8_t)(active!=0U);
    }
}

void garden_combat_observe(GardenCombat *c,const GardenGame *g)
{
    if (!c || !g) return;
    unsigned epoch=c->initialized && (g->tick<c->last_tick ||
        g->combat.emitted_total<c->last_emitted_total ||
        (g->mode==GARDEN_MODE_WAVES && c->last_mode!=GARDEN_MODE_WAVES));
    if (epoch) { clear_temporary(c); c->initialized=0U; }
    if (c->initialized && g->tick==c->last_tick) return;
    if (c->initialized) age_temporary(c,g->tick-c->last_tick);
    clear_reused_target_lanes(c,g);
    unsigned fresh=g->combat.tick==g->tick;
    unsigned count=fresh ? g->combat.count : 0U;
    if (count>GARDEN_COMBAT_EVENT_MAX) {
        c->stats.event_rejected+=count-GARDEN_COMBAT_EVENT_MAX;
        count=GARDEN_COMBAT_EVENT_MAX;
    }
    if (c->initialized) {
        uint32_t emitted=g->combat.emitted_total-c->last_emitted_total;
        if (emitted>count) c->stats.event_gaps+=emitted-count;
    }
    /* Group only this newly consumed logic step. Birth ownership prevents
     * a pool replacement from redirecting a later hit into another label. */
    uint8_t popup_index[GARDEN_MAX_ENEMIES+1U]={0};
    uint32_t popup_birth[GARDEN_MAX_ENEMIES+1U];
    for (unsigned i=0;i<count;i++) {
        const GardenHitEvent *e=&g->combat.items[i];
        if (!valid_event(e)) { c->stats.event_rejected++; continue; }
        c->stats.events_observed++;
        c->stats.kills_observed+=e->killed!=0U;
        c->stats.score_observed+=e->score_delta;
        c->stats.damage_observed+=e->damage;
        if (!c->enabled) continue;
        c->flash[e->target_slot]=(GardenCombatFlash){e->target_x,e->target_y,
            GARDEN_COMBAT_FLASH_TICKS,e->target_type};
        spawn_impact(c->impacts,GARDEN_COMBAT_IMPACT_MAX,e->x,e->y,16U,
                     &c->stats.impacts_spawned,&c->stats.impact_replaced);
        if (e->killed) spawn_impact(c->kill_rings,GARDEN_COMBAT_KILL_MAX,
            e->target_x,e->target_y,24U,&c->stats.kill_rings_spawned,&c->stats.kill_ring_replaced);
        unsigned slot=e->target_slot;
        GardenCombatPopup *p=popup_index[slot] ? &c->popups[popup_index[slot]-1U] : 0;
        if(p && p->active && p->target_type==e->target_type &&
           p->spawn_sequence==popup_birth[slot]){
            unsigned damage=(unsigned)p->damage+e->damage;
            if(damage>32767U){c->stats.damage_display_clamped+=damage-32767U;damage=32767U;}
            p->damage=(uint16_t)damage;p->killed|=(uint8_t)(e->killed!=0U);
            c->stats.popups_merged++;
        }else{
            unsigned index=spawn_popup(c,e);
            if(index<GARDEN_COMBAT_POPUP_MAX){
                popup_index[slot]=(uint8_t)(index+1U);
                popup_birth[slot]=c->popups[index].spawn_sequence;
            }
        }
    }
    c->initialized=1U;
    c->last_tick=g->tick; c->last_mode=g->mode;
    c->last_emitted_total=g->combat.emitted_total;
}

static unsigned essential_count(const GardenGame *g)
{
    unsigned n=g->boss.active!=0U;
    n+=g->player.active && (!g->player.grace || (g->tick&3U)<2U);
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) n+=g->enemies[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) n+=g->player_bullets[i].active!=0U;
    for (unsigned i=0;i<garden_enemy_scan_limit;i++) n+=g->enemy_bullets[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) n+=g->effects[i].active!=0U;
    return n;
}

static uint8_t opacity(unsigned enabled,unsigned a)
{ return (uint8_t)(enabled ? a : 32U); }

static unsigned popup_width(unsigned damage)
{
    unsigned digits=1U;
    while (damage>=10U) { damage/=10U; digits++; }
    return 12U+digits*16U;
}

static GardenDraw popup_draw(const GardenCombatPopup *p,unsigned alpha,unsigned fraction_q8)
{
    unsigned width=popup_width(p->damage);
    unsigned elapsed=(unsigned)p->age*256U+fraction_q8;
    unsigned u=elapsed/GARDEN_COMBAT_POPUP_TICKS; /* normalized timeQ8, 0..255 */
    if (u>256U) u=256U;
    unsigned ease=u*(512U-u); /* ease-out: 1-(1-t)^2, Q16 */
    unsigned rise=((unsigned)p->rise_budget*ease+32768U)>>16;
    unsigned fade=32U-(elapsed*32U)/(GARDEN_COMBAT_POPUP_TICKS*256U);
    if (fade>32U) fade=32U;
    if (!fade) fade=1U;
    /* The field/own-HP path budget was fixed at birth; no per-frame clamp or retargeting.
     * Full initial Alpha plus the fast first100ms of ease-out is the pop. */
    return (GardenDraw){(int16_t)(p->x-(int)width/2),
        (int16_t)(p->y-8-(int)rise),(uint16_t)(GARDEN_DRAW_SCORE_BASE+p->damage),opacity(alpha,fade)};
}

static unsigned choose_birth_anchor(GardenCombat *c,GardenCombatPopup *p,
                                     const GardenGame *g,const GardenScene *scene)
{
    unsigned width=popup_width(p->damage);
    int target_x=p->target_x/GARDEN_GRAIN,target_y=p->target_y/GARDEN_GRAIN;
    unsigned boss=p->slot==GARDEN_MAX_ENEMIES;
    unsigned asset=boss ? GARDEN_ASSET_BOSS1:
        p->target_type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1:GARDEN_ASSET_STOPPER1;
    const garden_sprite_t *sprite=garden_asset(asset);
    int model_top=target_y-(sprite ? sprite->anchor_y:0);
    int bar_top=target_y-(int)garden_actor_ui_bar_offset(asset);
    GardenDraw bar;
    /* The event position remains authoritative after a killing target goes
     * inactive. A live target can expose the exact shared HP/pose geometry. */
    if (garden_actor_ui_bar_geometry(g,p->slot,&bar)) bar_top=bar.y;
    if (boss) {
        GardenSceneBossPose pose;
        if (garden_scene_boss_pose(scene,g,&pose) && pose.top<model_top) model_top=pose.top;
    }
    if (bar_top<0) bar_top=0;
    if (bar_top>GARDEN_HEIGHT-8) bar_top=GARDEN_HEIGHT-8;
    int top=(model_top<bar_top ? model_top:bar_top)-18;
    unsigned budget;
    if (top<2) {
        /* Entry has no above-bar room: stay in the upper material region,
         * beginning8px below the final position. It never crosses the HP bar
         * and never changes to a side label. */
        top=bar_top+8+2+8;budget=8U;p->anchor_policy=2U;
    } else {
        if (top>GARDEN_HEIGHT-18) top=GARDEN_HEIGHT-18;
        budget=(unsigned)(top-2);
        if (budget>GARDEN_COMBAT_POPUP_RISE) budget=GARDEN_COMBAT_POPUP_RISE;
        p->anchor_policy=1U;
    }
    int left=target_x-(int)width/2;
    if (left<2) left=2;
    if (left>GARDEN_WIDTH-(int)width-2) left=GARDEN_WIDTH-(int)width-2;
    p->x=(int16_t)(left+(int)width/2);p->y=(int16_t)(top+8);
    p->rise_budget=(uint8_t)budget;p->anchor_ready=1U;p->anchor_visible=1U;
    c->stats.popup_anchors_chosen++;
    return 1U;
}

static void prepare_birth_anchors(GardenCombat *c,const GardenGame *g,const GardenScene *scene)
{
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) {
        GardenCombatPopup *p=&c->popups[i];
        if (p->active && !p->anchor_ready) choose_birth_anchor(c,p,g,scene);
    }
}

static unsigned selected_decor(GardenCombat *c,GardenDraw *out,
                               unsigned limit,unsigned alpha,unsigned *wanted,const GardenGame *g,const GardenScene *scene)
{
    unsigned count=0U;
    (void)g;(void)scene;
    *wanted=0U;
    /* Reserve actual HP labels before optional art. Paint the textured impact
     * first so neither ordinary nor killing-hit numbers get covered by a
     * later spark. Each valid event still owns its independent label. */
    unsigned labels=0U;
    for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++)
        labels+=c->popups[i].active && c->popups[i].anchor_visible;
    unsigned art_limit=limit>labels ? limit-labels:0U;
    for (unsigned i=0;i<GARDEN_COMBAT_KILL_MAX;i++) {
        const GardenCombatImpact *p=&c->kill_rings[i];
        if (!p->active) continue;
        (*wanted)++;
        if (count<art_limit) {
            unsigned frame=(unsigned)p->age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/GARDEN_COMBAT_KILL_TICKS;
            out[count++]=(GardenDraw){p->x,p->y,(uint16_t)(GARDEN_ASSET_ORDNANCE_HIT_BASE+frame),
                                     opacity(alpha,32U-p->age)};
            c->stats.kill_rings++;
        }
    }
    for (unsigned i=0;i<GARDEN_COMBAT_IMPACT_MAX;i++) {
        const GardenCombatImpact *p=&c->impacts[i];
        if (!p->active) continue;
        (*wanted)++;
        if (count<art_limit) {
            unsigned frame=(unsigned)p->age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/GARDEN_COMBAT_IMPACT_TICKS;
            out[count++]=(GardenDraw){p->x,p->y,(uint16_t)(GARDEN_ASSET_ORDNANCE_HIT_BASE+frame),
                                     32U};
            c->stats.impacts++; c->stats.impact_layers++;
        }
    }
    for (unsigned pass=0;pass<2U;pass++) for (unsigned i=0;i<GARDEN_COMBAT_POPUP_MAX;i++) {
        const GardenCombatPopup *p=&c->popups[i];
        if (!p->active || (p->killed!=0U)!=(pass==0U)) continue;
        if (!p->anchor_visible) { (*wanted)++;c->stats.popup_layout_dropped++;continue; }
        (*wanted)++;
        if (count<limit) { out[count++]=popup_draw(p,alpha,c->render_fraction_q8); c->stats.popups++; }
    }
    return count;
}

static unsigned held_flash(GardenCombat *c,GardenFx *fx,const GardenGame *g,
                           GardenDraw *draws,unsigned count)
{
    if (!c->enabled || !(fx->flags&GARDEN_FX_FLASH)) return 0U;
    unsigned found=0U;
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES+1U;i++) {
        if (i==GARDEN_MAX_ENEMIES) continue; /* Boss uses local core/hit feedback, never a whole-body WHITE swap. */
        const GardenCombatFlash *f=&c->flash[i];
        if (!f->remaining) continue;
        unsigned active,asset,white,type; int32_t x,y;
        if (i==GARDEN_MAX_ENEMIES) {
            active=g->boss.active; type=GARDEN_ENEMY_NONE;
            x=g->boss.x; y=g->boss.y; asset=GARDEN_ASSET_BOSS1; white=GARDEN_ASSET_FLASH_BOSS1;
        } else {
            const GardenEnemy *e=&g->enemies[i];
            active=e->active; type=e->type; x=e->x; y=e->y;
            asset=type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1 : GARDEN_ASSET_STOPPER1;
            white=type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_FLASH_DARTER1 : GARDEN_ASSET_FLASH_STOPPER1;
        }
        /* Never flash a reused slot which has changed type or teleported to
         * a new spawn. Ordinary movement over 150ms remains within 64px.
         */
        int64_t dx=(int64_t)x-f->x,dy=(int64_t)y-f->y;
        if (!active || type!=f->type || dx<-64000 || dx>64000 || dy<-64000 || dy>64000) continue;
        unsigned preceding=0U;
        if (i<GARDEN_MAX_ENEMIES) for (unsigned k=0;k<i;k++) {
            const GardenEnemy *e=&g->enemies[k];
            /* Essential enemy bodies preserve the core's stable slot order,
             * even if identical sprites occupy the exact same coordinate.
             * Skip those earlier bodies so the later unflashed body cannot
             * cover a flash accidentally applied to the wrong actor.
             */
            if (e->active && e->type==type &&
                e->x/GARDEN_GRAIN==x/GARDEN_GRAIN &&
                e->y/GARDEN_GRAIN==y/GARDEN_GRAIN) preceding++;
        }
        for (unsigned j=0;j<count;j++) {
            GardenDraw *d=&draws[j];
            if ((d->asset!=asset && d->asset!=white) ||
                d->x!=x/GARDEN_GRAIN || d->y!=y/GARDEN_GRAIN) continue;
            if (preceding) { preceding--; continue; }
            if (d->asset!=white) { d->asset=(uint16_t)white; fx->stats.flash_sprites++; }
            found++; break;
        }
    }
    return found;
}

unsigned garden_combat_build(GardenCombat *c,GardenScene *scene,
                            GardenFeedback *feedback,GardenFx *fx,
                            const GardenGame *g,GardenDraw *draws,
                            unsigned capacity,unsigned alpha,unsigned focus)
{
    if (!c || !scene || !feedback || !fx || !g) return 0U;
    if (!c->initialized) garden_combat_observe(c,g);
    unsigned limit=capacity<GARDEN_DRAW_MAX ? capacity : GARDEN_DRAW_MAX;
    if (!draws) limit=0U;
    prepare_birth_anchors(c,g,scene);
    GardenCombatStats *s=&c->stats;
    s->impacts=s->impact_layers=s->kill_rings=s->popups=s->held_flashes=s->marker=0U;
    unsigned essential=essential_count(g);
    unsigned marker_wanted=(focus || g->demo_mode) && g->player.active && !g->player.respawning;
    unsigned reserve_marker=marker_wanted && limit>essential;
    unsigned high=essential+reserve_marker;
    unsigned available=limit>high ? limit-high : 0U;
    GardenDraw strong[2U*GARDEN_COMBAT_IMPACT_MAX+GARDEN_COMBAT_KILL_MAX+GARDEN_COMBAT_POPUP_MAX];
    unsigned wanted=0U;
    unsigned strong_count=c->enabled ? selected_decor(c,strong,available,alpha,&wanted,g,scene) : 0U;
    /* The explicit rings/sparks supersede the former HP-delta observer's
     * small sparks. Preserve that observer's history for statistics/fallback
     * diagnosis, without rendering duplicate impacts on top of each other.
     */
    GardenHitSpark saved[GARDEN_FEEDBACK_SPARK_MAX];
    memcpy(saved,feedback->sparks,sizeof(saved));
    if (c->enabled) memset(feedback->sparks,0,sizeof(feedback->sparks));
    unsigned count=garden_scene_build(scene,feedback,fx,g,draws,limit-strong_count,alpha,focus);
    memcpy(feedback->sparks,saved,sizeof(saved));
    unsigned flashed_before=fx->stats.flash_sprites;
    s->held_flashes=held_flash(c,fx,g,draws,count);
    if (!flashed_before && fx->stats.flash_sprites) fx->stats.flash_frames++;
    /* Scene/feedback paints the real marker last. Temporarily remove that
     * entry, paint impacts after all projectiles, then restore it on top.
     */
    GardenDraw marker={0}; unsigned have_marker=0U;
    if (count && (draws[count-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE ||
                  draws[count-1U].asset==GARDEN_ASSET_FOCUS_GRACE)) {
        marker=draws[--count]; have_marker=1U;
    }
    for (unsigned i=0;i<strong_count && count<limit;i++) draws[count++]=strong[i];
    if (have_marker && count<limit) { draws[count++]=marker; s->marker=1U; }
    s->essential_dropped=scene->stats.essential_dropped;
    s->decorative_dropped=scene->stats.decorative_dropped+wanted-strong_count;
    s->builds++;
    s->impact_frames+=s->impacts!=0U; s->kill_frames+=s->kill_rings!=0U;
    s->popup_frames+=s->popups!=0U; s->flash_frames+=s->held_flashes!=0U;
    s->marker_frames+=s->marker!=0U;
    if (count>s->peak_draws) s->peak_draws=count;
    s->essential_drop_total+=s->essential_dropped;
    s->decorative_drop_total+=s->decorative_dropped;
    return count;
}

unsigned garden_combat_popup_draw(const GardenCombat *c,unsigned index,
                                  unsigned alpha,GardenDraw *draw)
{
    if (!c || !draw || index>=GARDEN_COMBAT_POPUP_MAX) return 0U;
    const GardenCombatPopup *p=&c->popups[index];
    if (!p->active || !p->anchor_ready || !p->anchor_visible) return 0U;
    *draw=popup_draw(p,alpha,c->render_fraction_q8);return 1U;
}

const GardenCombatStats *garden_combat_stats(const GardenCombat *combat)
{ return combat ? &combat->stats : 0; }
