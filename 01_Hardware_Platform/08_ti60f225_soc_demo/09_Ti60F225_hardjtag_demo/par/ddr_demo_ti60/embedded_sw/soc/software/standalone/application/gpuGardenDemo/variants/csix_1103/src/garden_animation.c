/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_animation.h"
#include "garden_assets.h"
#include <string.h>
#include <limits.h>

void garden_animation_reset(GardenAnimation *a)
{ if(a) memset(a,0,sizeof(*a)); }

static void remember(GardenAnimation *a,const GardenGame *g)
{
    a->initialized=1U;a->last_tick=g->tick;a->last_mode=g->mode;
    a->last_lives=g->lives;a->last_player_x=g->player.x;a->last_player_y=g->player.y;
}
static int16_t pixel(int32_t raw)
{
    int32_t p=raw/GARDEN_GRAIN;
    if(p>INT16_MAX) p=INT16_MAX;
    if(p<INT16_MIN) p=INT16_MIN;
    return (int16_t)p;
}

void garden_animation_observe(GardenAnimation *a,const GardenGame *g)
{
    if(!a || !g) return;
    a->paused=g->paused!=0U;
    if(!a->initialized || g->tick<a->last_tick ||
       (a->last_mode!=GARDEN_MODE_WAVES && g->mode==GARDEN_MODE_WAVES)) {
        a->death_active=0U;a->death_age=0U;a->fraction_q8=0U;
        remember(a,g);return;
    }
    if(g->tick==a->last_tick || a->paused) return;
    uint32_t elapsed=g->tick-a->last_tick;
    if(a->death_active) {
        if(elapsed>=GARDEN_ANIMATION_DEATH_TICKS-a->death_age) a->death_active=0U;
        else a->death_age=(uint8_t)(a->death_age+elapsed);
    }
    if(g->lives<a->last_lives && elapsed==1U) {
        int32_t x=a->last_player_x,y=a->last_player_y;
        /* The core moves the player to its respawn position immediately.
         * The fresh real death effect preserves the actual collision point.
         * Core adds player death after enemy/Boss kills, so prefer the last
         * nearby age0 allocation and never reuse last-step age1 explosions.
         * If the existing effect pool is full, previous observed position is
         * a bounded fallback (the core emits no separate player-death point). */
        for(unsigned i=0U;i<GARDEN_MAX_EFFECTS;i++) {
            const GardenEffect *e=&g->effects[i];
            if(!e->active || !e->big || e->age!=0U) continue;
            int64_t dx=(int64_t)e->x-a->last_player_x,dy=(int64_t)e->y-a->last_player_y;
            if(dx>=-32000 && dx<=32000 && dy>=-32000 && dy<=32000) { x=e->x;y=e->y; }
        }
        a->death_x=pixel(x);a->death_y=pixel(y);a->death_age=0U;a->death_active=1U;
        a->stats.death_triggers++;
    }
    remember(a,g);
}

void garden_animation_set_fraction(GardenAnimation *a,unsigned fraction_q8)
{ if(a && !a->paused) a->fraction_q8=fraction_q8>255U ? 255U:fraction_q8; }

static int16_t add_pixel(int16_t p,int delta)
{
    int value=(int)p+delta;
    if(value>INT16_MAX) value=INT16_MAX;
    if(value<INT16_MIN) value=INT16_MIN;
    return (int16_t)value;
}

unsigned garden_animation_apply(GardenAnimation *a,const GardenGame *g,
                                GardenDraw *draws,unsigned count,unsigned capacity,
                                unsigned alpha_enabled)
{
    if(!a || !g || (!draws && capacity)) return count;
    if(capacity>GARDEN_DRAW_MAX) capacity=GARDEN_DRAW_MAX;
    if(count>capacity) count=capacity;
    a->stats.fragments=a->stats.enemy_bullets=0U;
    a->stats.camera_x=a->stats.camera_y=0;
    for(unsigned i=0U;i<count;i++) {
        unsigned id=draws[i].asset;
        if(garden_asset_is_enemy_bullet(garden_draw_form_source(id))) a->stats.enemy_bullets++;
        /* Preserve the selected colour family and rotation, including its
         * original source pixels and opacity. remapped_total remains zero. */
    }
    if(a->stats.enemy_bullets) a->stats.danger_bullet_frames++;
    if(a->death_active) {
        static const int16_t vx[8]={-640,640,-384,384,-256,256,-512,512};
        static const int16_t vy[8]={-128,-128,-640,-640,384,384,-384,-384};
        unsigned time=(unsigned)a->death_age*256U+a->fraction_q8;
        if(time>GARDEN_ANIMATION_DEATH_TICKS*256U) time=GARDEN_ANIMATION_DEATH_TICKS*256U;
        unsigned alpha=32U-time*32U/(GARDEN_ANIMATION_DEATH_TICKS*256U);
        if(!alpha) alpha=1U;
        unsigned available=capacity-count,kept=available<GARDEN_ANIMATION_FRAGMENT_MAX ? available:GARDEN_ANIMATION_FRAGMENT_MAX;
        unsigned insert=count;
        /* Never cover the real vulnerability/protection marker. Particles
         * are visual only and cannot force any actual object out of the list. */
        for(unsigned i=0U;i<count;i++) if(draws[i].asset==GARDEN_ASSET_FOCUS_VULNERABLE ||
                                         draws[i].asset==GARDEN_ASSET_FOCUS_GRACE) { insert=i;break; }
        if(kept) memmove(draws+insert+kept,draws+insert,(count-insert)*sizeof(*draws));
        for(unsigned i=0U;i<kept;i++) {
            int dx=(int)vx[i]*(int)time/65536;
            int dy=(int)vy[i]*(int)time/65536+(int)((time*time)>>21U);
            draws[insert+i]=(GardenDraw){add_pixel(a->death_x,dx),add_pixel(a->death_y,dy),
                GARDEN_ASSET_HIT_SPARK,
                (uint8_t)(alpha_enabled ? alpha:32U)};
        }
        count+=kept;a->stats.fragments=kept;a->stats.fragment_frames+=kept!=0U;
        a->stats.fragment_dropped+=GARDEN_ANIMATION_FRAGMENT_MAX-kept;a->stats.death_frames++;
        /* One bounded field offset during only the protected death moment.
         * Ordinary vulnerable movement/firing NEVER shakes hazards. All
         * world objects, HP/text and real player marker share the same offset;
         * the renderer's fixed playfield clip keeps the cockpit still. */
        if(a->death_age<GARDEN_ANIMATION_SHAKE_TICKS &&
           (g->player.respawning || g->player.grace || g->mode==GARDEN_MODE_GAMEOVER)) {
            static const int8_t sx[4]={1,-1,1,0},sy[4]={0,1,-1,0};
            unsigned phase=(time/128U)&3U;
            int amplitude=a->death_age<2U ? 4:2;
            int dx=sx[phase]*amplitude,dy=sy[phase]*amplitude;
            for(unsigned i=0U;i<count;i++) { draws[i].x=add_pixel(draws[i].x,dx);draws[i].y=add_pixel(draws[i].y,dy); }
            a->stats.camera_x=dx;a->stats.camera_y=dy;
            a->stats.camera_frames+=(dx || dy)!=0;
        }
    }
    /* Protection is a real core grace state. Do not hide vulnerable hazards,
     * remap danger colours, or synthesize a safe time from an animation. */
    if(g->player.active && !g->player.respawning && g->player.grace) {
        for(unsigned i=0U;i<count;i++) if(draws[i].asset==GARDEN_ASSET_FOCUS_GRACE) {
            draws[i].alpha=(uint8_t)(alpha_enabled ? 16U+((g->tick&7U)*16U)/7U:32U);
            a->stats.protection_frames++;
            break;
        }
    }
    /* A thrown entity now owns its real position/impact point. Its dedicated
     * view draws the projectile and local textured detonation; there is no
     * player-centred radial clearance/protection effect to misstate a rule. */
    if(g->ordnance.active) a->stats.bomb_frames++;
    a->stats.builds++;
    if(count>a->stats.peak_draws) a->stats.peak_draws=count;
    return count;
}
const GardenAnimationStats *garden_animation_stats(const GardenAnimation *a)
{ return a ? &a->stats:0; }
