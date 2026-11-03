/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_motion.h"
#include "garden_assets.h"
#include "garden_heading_precision.h"
#include "garden_fx.h"
#include <limits.h>
#include <string.h>
typedef char motion_slot_is12bytes[sizeof(GardenMotionSlot)==12U ? 1:-1];
/* Entire motion state resides in the guarded32KiB DDR block. */
typedef char motion_state_bounded[sizeof(GardenMotion)<=0x8000U ? 1:-1];

void garden_motion_reset(GardenMotion *m)
{ if(m) memset(m,0,sizeof(*m)); }
void garden_motion_set_fraction(GardenMotion *m,unsigned q8)
{ if(m && !m->paused) m->fraction_q8=(uint16_t)(q8>255U ? 255U:q8); }

/* Exact trunc(raw*16/1000), without an RV32 divide. Magnitude is unsigned,
 * so INT32_MIN is safe before the range guard. For n=2*mag<=4096124:
 * 34359739*125=2^32+79 and n*79<2^32. The reciprocal error is therefore
 * <1/125, smaller than any nonzero fractional distance to the next quotient.
 * The high32 product equals floor(n/125) for the entire accepted range. */
static unsigned q4(int32_t raw,int16_t *result)
{
    unsigned negative=raw<0;
    uint32_t magnitude=negative ? 0U-(uint32_t)raw:(uint32_t)raw;
    if(magnitude>2048062U) return 0U;
    uint32_t quotient=(uint32_t)(((uint64_t)(magnitude*2U)*34359739U)>>32);
    if(quotient>(negative ? 32768U:32767U)) return 0U;
    *result=(int16_t)(negative ? -(int32_t)quotient:(int32_t)quotient);
    return 1U;
}
#ifdef GARDEN_MOTION_HOST_Q4_PROBE
/* Host contract seam; absent from the production object and header/API. */
unsigned garden_motion_host_q4(int32_t raw,int16_t *result)
{ return q4(raw,result); }
#endif
static unsigned enemy_tag(const GardenEnemyBullet *b)
{
    if(!b->active || b->type<1U || b->type>2U || b->colour<1U || b->colour>5U) return 0U;
    return 0x80U|((unsigned)b->type<<3)|b->colour;
}
static void observe_slot(GardenMotion *m,GardenMotionSlot *slot,int32_t x,int32_t y,
                         unsigned age,unsigned tag,unsigned epoch)
{
    int16_t nx,ny;
    if(!tag) { slot->tag=slot->smooth=0U;return; }
    if(!q4(x,&nx) || !q4(y,&ny)) {
        slot->tag=slot->smooth=0U;m->stats.range_rejected++;return;
    }
    int dx=(int)nx-slot->current_x,dy=(int)ny-slot->current_y;
    unsigned same=!epoch && slot->tag==tag && age>1U && age==(unsigned)slot->age+1U;
    unsigned distant=dx>1024 || dx<-1024 || dy>1024 || dy<-1024;
    /* Q4 endpoints each omit<1unit per axis. A63.875px conservative
     * threshold ensures every true>64px teleport snaps, without extra raw
     * coordinate storage. Ordinary movement is far below this guard. */
    if(!distant) distant=dx*dx+dy*dy>=1022*1022;
    if(same && distant) m->stats.teleports++;
    if(!epoch && slot->tag && !same) m->stats.reused++;
    slot->smooth=(uint8_t)(same && !distant);
    slot->previous_x=slot->smooth ? slot->current_x:nx;
    slot->previous_y=slot->smooth ? slot->current_y:ny;
    slot->current_x=nx;slot->current_y=ny;
    slot->age=(uint16_t)age;slot->tag=(uint8_t)tag;
}
void garden_motion_observe(GardenMotion *m,const GardenGame *g)
{
    if(!m || !g) return;
    m->paused=(uint8_t)(g->paused!=0U);
    if(m->initialized && g->tick==m->last_tick && g->mode==m->last_mode) return;
    unsigned epoch=!m->initialized || g->tick!=m->last_tick+1U || g->mode!=m->last_mode;
    if(epoch) {
        if(m->initialized) m->stats.epochs++;
        m->fraction_q8=0U;
    }
    for(unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) {
        const GardenPlayerBullet *b=&g->player_bullets[i];
        observe_slot(m,&m->player[i],b->x,b->y,b->age,b->active ? 1U:0U,epoch);
    }
    for(unsigned i=0;i<garden_enemy_scan_limit;i++) {
        const GardenEnemyBullet *b=&g->enemy_bullets[i];
        observe_slot(m,&m->enemy[i],b->x,b->y,b->age,enemy_tag(b),epoch);
    }
    unsigned pilot_tag=g->player.active && !g->player.respawning ? 1U:0U;
    observe_slot(m,&m->pilot,g->player.x,g->player.y,(uint16_t)g->tick,pilot_tag,
                 epoch || g->lives<m->last_lives);
    m->last_lives=g->lives;
    m->last_tick=g->tick;m->last_mode=g->mode;m->initialized=1U;m->stats.observations++;
}
/* The final raster has integer pixels. Round the interpolatedQ4 coordinate
 * once, symmetrically around zero; the error from raw linear interpolation
 * stays below1pixel, with no per-layer intermediate integer-pixel rounding. */
static int16_t interpolated_pixel(int16_t before,int16_t current,unsigned q8)
{
    int value=(int)before+((int)current-before)*(int)q8/256;
    return (int16_t)(value<0 ? -((-value+8)/16):(value+8)/16);
}
static unsigned apply_one(GardenMotion *m,const GardenGame *g,GardenMotionSlot *slot,
                          unsigned tag,unsigned age,int32_t x,int32_t y,
                          unsigned asset,unsigned alpha,GardenDraw *draw)
{
    /* A mismatch never moves an unrecognized/decorative entry. The caller's
     * current full-opacity sprite must identify this exact active pool slot. */
    if(draw->asset!=asset || draw->alpha!=alpha) {
        m->stats.match_rejected++;return 0U;
    }
    int16_t cx=0,cy=0;
    unsigned coordinates_valid=q4(x,&cx) && q4(y,&cy);
    /* Truncating an exact Q4 conversion to whole pixels is identical to
     * raw/1000. Reuse it here instead of two more slow CPU divisions per
     * projectile/frame. Malformed/out-of-range inputs keep the old path. */
    int expected_x=coordinates_valid ? cx/16:x/GARDEN_GRAIN;
    int expected_y=coordinates_valid ? cy/16:y/GARDEN_GRAIN;
    if(draw->x!=expected_x || draw->y!=expected_y) {
        m->stats.match_rejected++;return 0U;
    }
    if(!m->initialized || m->last_tick!=g->tick || !tag || slot->tag!=tag || slot->age!=age ||
       !coordinates_valid || cx!=slot->current_x || cy!=slot->current_y) {
        m->stats.snapped++;return 1U;
    }
    if(slot->smooth) {
        draw->x=interpolated_pixel(slot->previous_x,slot->current_x,m->fraction_q8);
        draw->y=interpolated_pixel(slot->previous_y,slot->current_y,m->fraction_q8);
        m->stats.interpolated++;
    } else m->stats.snapped++;
    return 1U;
}
/* Decode only the expected type/colour's exact legacy8 or appended56 IDs.
 * Each appended octant has7frames: floor(rank/7)==(rank*37)>>8 for rank0..55.
 * This avoids an RV32 divide while reconstructing the unchanged atlas angle.
 * Caller still checks both exact velocity bounds and the public ID helper. */
static unsigned enemy_draw_angle(const GardenEnemyBullet *b,unsigned asset)
{
    unsigned colour=b->colour;
    if(colour<1U || colour>5U) colour=3U;
    unsigned diamond=b->type==2U;
    unsigned legacy=(diamond ? GARDEN_ASSET_DIAMOND_BASE:GARDEN_ASSET_DART_BASE)+(colour-1U)*8U;
    if(asset-legacy<8U) return (asset-legacy)*128U;
    unsigned rank=asset-(280U+diamond*280U+(colour-1U)*56U);
    if(rank>=56U) return 1024U;
    return (rank+((rank*37U)>>8)+1U)*16U;
}
unsigned garden_motion_apply_bullets(GardenMotion *m,const GardenGame *g,
                                    GardenDraw *draws,unsigned count)
{
    if(!m || !g || !draws) return 0U;
    if(count>GARDEN_DRAW_MAX) count=GARDEN_DRAW_MAX;
    m->stats.player_bullets=m->stats.enemy_bullets=0U;
    m->stats.interpolated=m->stats.snapped=0U;m->stats.builds++;
    unsigned player=0U,enemy=0U;
    for(unsigned i=0;i<count;i++) {
        GardenDraw *d=&draws[i];
        unsigned source_asset=garden_draw_form_source(d->asset);
        unsigned body=d->asset==GARDEN_ASSET_PLAYER || d->asset==GARDEN_ASSET_PLAYER_FRAME1 ||
                      d->asset==GARDEN_ASSET_PLAYER_FRAME2 || (d->asset&~31U)==GARDEN_DRAW_BANK_BASE;
        unsigned marker=d->asset==GARDEN_ASSET_FOCUS_VULNERABLE || d->asset==GARDEN_ASSET_FOCUS_GRACE;
        if((body || marker) && g->player.active && !g->player.respawning && d->alpha==32U &&
           d->x==g->player.x/GARDEN_GRAIN && d->y==g->player.y/GARDEN_GRAIN) {
            (void)apply_one(m,g,&m->pilot,1U,(uint16_t)g->tick,g->player.x,g->player.y,d->asset,32U,d);
        }
        if(d->asset==GARDEN_ASSET_SHOT) {
            while(player<GARDEN_MAX_PLAYER_BULLETS && !g->player_bullets[player].active) player++;
            if(player<GARDEN_MAX_PLAYER_BULLETS) {
                const GardenPlayerBullet *b=&g->player_bullets[player];
                if(apply_one(m,g,&m->player[player],1U,b->age,b->x,b->y,GARDEN_ASSET_SHOT,32U,d)) {
                    m->stats.player_bullets++;player++;
                }
            }
        } else if(garden_asset_is_enemy_bullet(source_asset)) {
            while(enemy<garden_enemy_scan_limit && !g->enemy_bullets[enemy].active) enemy++;
            if(enemy<garden_enemy_scan_limit) {
                const GardenEnemyBullet *b=&g->enemy_bullets[enemy];
                unsigned angle=enemy_draw_angle(b,source_asset),id=UINT_MAX;
                if(angle<1024U && heading64_matches(b->vx,b->vy,b->cord_angle,angle))
                    id=garden_fx_enemy_asset(b,garden_bullet_precision_id(b->type,b->colour,angle));
                if(apply_one(m,g,&m->enemy[enemy],enemy_tag(b),b->age,b->x,b->y,id,garden_fx_enemy_alpha(b),d)) {
                    m->stats.enemy_bullets++;enemy++;
                }
            }
        }
    }
    return m->stats.player_bullets+m->stats.enemy_bullets;
}
const GardenMotionStats *garden_motion_stats(const GardenMotion *m)
{ return m ? &m->stats:0; }
