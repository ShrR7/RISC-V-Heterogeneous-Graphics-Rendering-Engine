/* SPDX-License-Identifier: GPL-3.0-or-later
 * Flash replacements, true prior-position ghosts, and small explosion rings.
 * No RNG, collision, HP, score or game clock writes occur in this module.
 */
#include "garden_fx.h"
#include "garden_assets.h"
#include <string.h>

static int same_position(GardenFxPoint a, GardenFxPoint b)
{ return a.x==b.x && a.y==b.y; }

static void clear_history(GardenFx *fx)
{
    fx->initialized=0U;
    fx->current=(GardenFxPoint){0};
    fx->previous[0]=fx->previous[1]=(GardenFxPoint){0};
}

void garden_fx_reset(GardenFx *fx)
{
    if (!fx) return;
    memset(fx,0,sizeof(*fx));
    fx->flags=GARDEN_FX_ALL;
}

void garden_fx_set_flags(GardenFx *fx,unsigned flags)
{
    if (!fx) return;
    flags&=GARDEN_FX_ALL;
    if (flags!=fx->flags) { clear_history(fx); fx->flags=flags; }
}

void garden_fx_observe(GardenFx *fx,const GardenGame *game)
{
    if (!fx || !game) return;
    GardenFxPoint point={
        (int16_t)(game->player.x/GARDEN_GRAIN),
        (int16_t)(game->player.y/GARDEN_GRAIN),
        (uint16_t)garden_player_id(game->tick),
        (uint8_t)(game->player.active && !game->player.respawning &&
                  !game->player.grace && game->mode<=GARDEN_MODE_BOSS)
    };
    if (!fx->initialized || game->tick<fx->last_tick ||
        game->mode!=fx->last_mode || !point.valid || !fx->current.valid) {
        fx->previous[0]=fx->previous[1]=(GardenFxPoint){0};
        fx->current=point;
    } else if (game->tick!=fx->last_tick) {
        /* A paused game has no new tick: its picture and ghosts stay frozen. */
        fx->previous[1]=fx->previous[0];
        fx->previous[0]=fx->current;
        fx->current=point;
    }
    fx->initialized=1U;
    fx->last_tick=game->tick;
    fx->last_mode=game->mode;
}

static void append(GardenDraw *draws,unsigned *count,unsigned limit,GardenDraw draw,
                   GardenFxStats *stats)
{
    if (*count<limit) draws[(*count)++]=draw;
    else stats->essential_dropped++;
}

static unsigned flashed_asset(GardenFx *fx,unsigned asset,unsigned flash)
{
    /* Boss retains readable materials during sustained hits. Local impact
     * sparks and the reactor pulse provide feedback instead of a83x117white
     * silhouette. Ordinary enemies retain their original flash policy.
     */
    if (asset==GARDEN_ASSET_BOSS1) return asset;
    if (!(fx->flags&GARDEN_FX_FLASH) || !flash) return asset;
    fx->stats.flash_sprites++;
    if (asset==GARDEN_ASSET_DARTER1) return GARDEN_ASSET_FLASH_DARTER1;
    if (asset==GARDEN_ASSET_STOPPER1) return GARDEN_ASSET_FLASH_STOPPER1;
    return GARDEN_ASSET_FLASH_BOSS1;
}

static unsigned ring_priority(const GardenEffect *effect)
{ return (effect->big ? 32U : 0U)+12U-effect->age; }

unsigned garden_fx_bullet_heading(int32_t vx,int32_t vy,unsigned fallback_angle)
{
    /* Unsigned negation safely accommodates INT32_MIN. Exact22.5-degree
     * boundaries need no atan, floating point, divide or normalization loop:
     * ax>=ay is horizontal iff (ax-ay)^2>=2*ay^2. All magnitudes<=2^31,
     * so each square<=2^62 and the doubled square<=2^63 fits uint64_t. */
    uint32_t ax=vx<0 ? 0U-(uint32_t)vx : (uint32_t)vx;
    uint32_t ay=vy<0 ? 0U-(uint32_t)vy : (uint32_t)vy;
    unsigned octant;
    if(!ax && !ay) return ((fallback_angle+64U)&1023U)&~127U;
    if(ax>=ay) {
        uint32_t difference=ax-ay;
        octant=(uint64_t)difference*difference>=2U*((uint64_t)ay*ay) ? 0U:1U;
    } else {
        uint32_t difference=ay-ax;
        octant=(uint64_t)difference*difference>=2U*((uint64_t)ax*ax) ? 2U:1U;
    }
    if(vx<0) octant=4U-octant;
    if(vy<0) octant=(8U-octant)&7U;
    return octant*128U;
}

unsigned garden_fx_build(GardenFx *fx,const GardenGame *game,GardenDraw *draws,
                         unsigned capacity,unsigned alpha_enabled)
{
    if (!fx || !game) return 0U;
    if (!fx->initialized) garden_fx_observe(fx,game);
    unsigned limit=GARDEN_DRAW_MAX-(alpha_enabled ? 1U : 0U),n=0;
    if (capacity<limit) limit=capacity;
    if (!draws) limit=0U;
    GardenFxStats *stats=&fx->stats;
    stats->flash_sprites=stats->ghosts=stats->rings=0U;
    stats->decorative_dropped=stats->essential_dropped=0U;
    /* Essentials are admitted first, in exactly the baseline object order. */
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) if (game->enemies[i].active) {
        const GardenEnemy *e=&game->enemies[i];
        unsigned asset=e->type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1 : GARDEN_ASSET_STOPPER1;
        append(draws,&n,limit,(GardenDraw){e->x/GARDEN_GRAIN,e->y/GARDEN_GRAIN,
            (uint16_t)flashed_asset(fx,asset,e->flash),32U},stats);
    }
    if (game->boss.active) append(draws,&n,limit,(GardenDraw){
        game->boss.x/GARDEN_GRAIN,game->boss.y/GARDEN_GRAIN,
        (uint16_t)flashed_asset(fx,GARDEN_ASSET_BOSS1,game->boss.flash),32U},stats);
    if (game->player.active && (!game->player.grace || (game->tick&3U)<2U))
        append(draws,&n,limit,(GardenDraw){game->player.x/GARDEN_GRAIN,
            game->player.y/GARDEN_GRAIN,(uint16_t)garden_player_id(game->tick),32U},stats);
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) if (game->player_bullets[i].active) {
        const GardenPlayerBullet *b=&game->player_bullets[i];
        append(draws,&n,limit,(GardenDraw){b->x/GARDEN_GRAIN,b->y/GARDEN_GRAIN,GARDEN_ASSET_SHOT,32U},stats);
    }
    for (unsigned i=0;i<garden_enemy_scan_limit;i++) if (game->enemy_bullets[i].active) {
        const GardenEnemyBullet *b=&game->enemy_bullets[i];
        unsigned asset=garden_bullet_precision_id(b->type,b->colour,
                heading64_from_velocity(b->vx,b->vy,b->cord_angle));
        append(draws,&n,limit,(GardenDraw){b->x/GARDEN_GRAIN,b->y/GARDEN_GRAIN,
            (uint16_t)garden_fx_enemy_asset(b,asset),(uint8_t)garden_fx_enemy_alpha(b)},stats);
    }
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) if (game->effects[i].active) {
        const GardenEffect *e=&game->effects[i];
        unsigned frame=(unsigned)e->age*GARDEN_ASSET_ORDNANCE_BLAST_COUNT/20U;
        if (frame>=GARDEN_ASSET_ORDNANCE_BLAST_COUNT) frame=GARDEN_ASSET_ORDNANCE_BLAST_COUNT-1U;
        append(draws,&n,limit,(GardenDraw){e->x/GARDEN_GRAIN,e->y/GARDEN_GRAIN,
            (uint16_t)(GARDEN_ASSET_ORDNANCE_BLAST_BASE+frame),32U},stats);
    }

    GardenDraw ghosts[2],rings[2],decor[4];
    unsigned ghost_count=0U,ring_count=0U,ring_candidates=0U;
    if (alpha_enabled && (fx->flags&GARDEN_FX_TRAIL) && fx->current.valid) {
        GardenFxPoint newer=fx->previous[0],older=fx->previous[1];
        if (older.valid && !same_position(older,fx->current) &&
            (!newer.valid || !same_position(older,newer)))
            ghosts[ghost_count++]=(GardenDraw){older.x,older.y,older.asset,6U};
        if (newer.valid && !same_position(newer,fx->current))
            ghosts[ghost_count++]=(GardenDraw){newer.x,newer.y,newer.asset,12U};
    }
    if (alpha_enabled && (fx->flags&GARDEN_FX_RING)) {
        int selected[2]={-1,-1};
        for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) {
            const GardenEffect *e=&game->effects[i];
            if (!e->active || e->age>=12U) continue;
            ring_candidates++;
            if (selected[0]<0 || ring_priority(e)>ring_priority(&game->effects[selected[0]])) {
                selected[1]=selected[0]; selected[0]=(int)i;
            } else if (selected[1]<0 || ring_priority(e)>ring_priority(&game->effects[selected[1]])) selected[1]=(int)i;
        }
        for (unsigned i=0;i<2U;i++) if (selected[i]>=0) {
            const GardenEffect *e=&game->effects[selected[i]];
            unsigned phase=(unsigned)e->age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/12U;
            unsigned alpha=28U-e->age;
            rings[ring_count++]=(GardenDraw){e->x/GARDEN_GRAIN,e->y/GARDEN_GRAIN,
                (uint16_t)(GARDEN_ASSET_ORDNANCE_HIT_BASE+phase),
                (uint8_t)alpha};
        }
    }
    /* Budget ghosts first; if only one fits, retain the brighter/newer one.
     * Painting rings and ghosts BEFORE essentials keeps projectile cores clear.
     */
    unsigned available=limit-n;
    unsigned kept_ghosts=ghost_count<available ? ghost_count : available;
    available-=kept_ghosts;
    unsigned kept_rings=ring_count<available ? ring_count : available;
    unsigned d=0U;
    for (unsigned i=0;i<kept_rings;i++) decor[d++]=rings[i];
    for (unsigned i=ghost_count-kept_ghosts;i<ghost_count;i++) decor[d++]=ghosts[i];
    if (d) {
        memmove(draws+d,draws,n*sizeof(*draws));
        memcpy(draws,decor,d*sizeof(*draws));
        n+=d;
    }
    stats->ghosts=kept_ghosts; stats->rings=kept_rings;
    stats->decorative_dropped=ghost_count-kept_ghosts+ring_candidates-kept_rings;
    if (n>stats->peak_draws) stats->peak_draws=n;
    stats->builds++;
    if (stats->flash_sprites) stats->flash_frames++;
    if (stats->ghosts) stats->trail_frames++;
    if (stats->rings) stats->ring_frames++;
    stats->decorative_drop_total+=stats->decorative_dropped;
    stats->essential_drop_total+=stats->essential_dropped;
    return n;
}

const GardenFxStats *garden_fx_stats(const GardenFx *fx)
{ return fx ? &fx->stats : 0; }
