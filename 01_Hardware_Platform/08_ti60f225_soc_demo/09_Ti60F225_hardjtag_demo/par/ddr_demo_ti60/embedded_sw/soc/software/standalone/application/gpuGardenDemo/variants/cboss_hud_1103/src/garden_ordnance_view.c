/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_ordnance_view.h"
#include "garden_assets.h"
#include <string.h>

static uint8_t opacity(unsigned enabled,unsigned value)
{ return (uint8_t)(enabled ? value:32U); }

unsigned garden_ordnance_laser_count(const GardenGame *g)
{
    return g && g->boss.active && g->mode==GARDEN_MODE_BOSS &&
           g->laser.phase>=GARDEN_LASER_WARN && g->laser.phase<=GARDEN_LASER_FADE ? 2U:0U;
}

unsigned garden_ordnance_laser_build(const GardenGame *g,GardenDraw *out,
                                    unsigned capacity,unsigned alpha_enabled)
{
    if(!out || capacity<2U || !garden_ordnance_laser_count(g)) return 0U;
    unsigned value=GARDEN_DRAW_LASER_BASE|(2U*GARDEN_LASER_HALF_WIDTH/GARDEN_GRAIN+1U);
    unsigned alpha;
    if(g->laser.phase==GARDEN_LASER_WARN) {
        unsigned remaining=g->laser.ticks<GARDEN_LASER_WARN_TICKS ? g->laser.ticks:GARDEN_LASER_WARN_TICKS;
        unsigned progress=GARDEN_LASER_WARN_TICKS-remaining;
        alpha=4U+progress*24U/GARDEN_LASER_WARN_TICKS;
    } else if(g->laser.phase==GARDEN_LASER_FIRE) {
        value|=GARDEN_DRAW_LASER_ACTIVE;alpha=32U;
    } else {
        unsigned remaining=g->laser.ticks<GARDEN_LASER_FADE_TICKS ? g->laser.ticks:GARDEN_LASER_FADE_TICKS;
        unsigned progress=GARDEN_LASER_FADE_TICKS-remaining;
        value|=GARDEN_DRAW_LASER_FADE;alpha=24U-progress*22U/GARDEN_LASER_FADE_TICKS;
    }
    if((g->laser.ticks/2U)&1U) value|=GARDEN_DRAW_LASER_FRAME;
    value|=(g->tick&7U)<<GARDEN_DRAW_LASER_SCROLL_SHIFT;
    for(unsigned i=0U;i<2U;i++) out[i]=(GardenDraw){
        (int16_t)(g->laser.x[i]/GARDEN_GRAIN),(int16_t)(g->laser.y/GARDEN_GRAIN),
        (uint16_t)value,opacity(alpha_enabled,alpha)};
    return 2U;
}

unsigned garden_ordnance_muzzle_build(const GardenGame *g,GardenDraw *out,
                                     unsigned capacity,unsigned alpha_enabled)
{
    if(!out || capacity<2U || !garden_ordnance_laser_count(g)) return 0U;
    unsigned age,asset,alpha;int offset=0,drop=0;
    if(g->laser.phase==GARDEN_LASER_WARN) {
        unsigned remaining=g->laser.ticks<GARDEN_LASER_WARN_TICKS ? g->laser.ticks:GARDEN_LASER_WARN_TICKS;
        age=GARDEN_LASER_WARN_TICKS-remaining;
        offset=(int)(remaining*20U/GARDEN_LASER_WARN_TICKS);
        drop=-(int)(remaining*12U/GARDEN_LASER_WARN_TICKS);
        alpha=4U+age*28U/GARDEN_LASER_WARN_TICKS;
        asset=GARDEN_ASSET_ORDNANCE_HIT_BASE+(age/3U)%GARDEN_ASSET_ORDNANCE_HIT_COUNT;
    } else if(g->laser.phase==GARDEN_LASER_FIRE) {
        unsigned remaining=g->laser.ticks<GARDEN_LASER_FIRE_TICKS ? g->laser.ticks:GARDEN_LASER_FIRE_TICKS;
        age=GARDEN_LASER_FIRE_TICKS-remaining;alpha=32U;
        asset=age<8U ? GARDEN_ASSET_ORDNANCE_BLAST_BASE+age/2U :
                      GARDEN_ASSET_ORDNANCE_HIT_BASE+(age/2U)%GARDEN_ASSET_ORDNANCE_HIT_COUNT;
    } else {
        unsigned remaining=g->laser.ticks<GARDEN_LASER_FADE_TICKS ? g->laser.ticks:GARDEN_LASER_FADE_TICKS;
        age=GARDEN_LASER_FADE_TICKS-remaining;drop=(int)age*2;
        alpha=24U-age*22U/GARDEN_LASER_FADE_TICKS;
        asset=GARDEN_ASSET_ORDNANCE_HIT_BASE+age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/GARDEN_LASER_FADE_TICKS;
    }
    for(unsigned i=0U;i<2U;i++) out[i]=(GardenDraw){
        (int16_t)(g->laser.x[i]/GARDEN_GRAIN+(i ? offset:-offset)),
        (int16_t)(g->laser.y/GARDEN_GRAIN+drop),(uint16_t)asset,opacity(alpha_enabled,alpha)};
    return 2U;
}

unsigned garden_ordnance_view_reserve(const GardenGame *g)
{
    if(!g) return 0U;
    unsigned n=g->ordnance.active ? (g->ordnance.blast_ticks ? 11U:1U):0U;
    for(unsigned i=0U;i<GARDEN_MAX_PICKUPS;i++) n+=g->pickups[i].active!=0U;
    n+=g->reward.pickup_flash_ticks!=0U;
    return n;
}

static unsigned emit(GardenDraw *out,unsigned n,unsigned capacity,int32_t x,int32_t y,
                      unsigned asset,unsigned alpha)
{
    if(n<capacity) out[n++]=(GardenDraw){(int16_t)(x/GARDEN_GRAIN),
        (int16_t)(y/GARDEN_GRAIN),(uint16_t)asset,(uint8_t)alpha};
    return n;
}

unsigned garden_ordnance_view_build(const GardenGame *g,GardenDraw *out,
                                   unsigned count,unsigned capacity,unsigned alpha_enabled)
{
    if(!g || (!out && capacity)) return count;
    if(capacity>GARDEN_DRAW_MAX) capacity=GARDEN_DRAW_MAX;
    if(count>capacity) count=capacity;
    unsigned slots=garden_ordnance_view_reserve(g),available=capacity-count;
    if(slots>available) slots=available;
    if(!slots) return count;
    unsigned at=count;
    /* World items and explosions sit behind all projectile cores and actual
     * HP-damage labels. Do not turn a hidden art particle into a hidden hazard. */
    for(unsigned i=0U;i<count;i++) {
        unsigned asset=out[i].asset;
        if(asset==GARDEN_ASSET_SHOT || garden_asset_is_enemy_bullet(garden_draw_form_source(asset)) ||
           (asset&GARDEN_DRAW_SCORE_BASE) || asset==GARDEN_ASSET_FOCUS_VULNERABLE ||
           asset==GARDEN_ASSET_FOCUS_GRACE) { at=i;break; }
    }
    memmove(out+at+slots,out+at,(count-at)*sizeof(*out));
    unsigned n=0U;
    GardenDraw *add=out+at;
    /* Bomb and healing entities take admission priority over optional debris. */
    if(g->ordnance.active) {
        if(g->ordnance.blast_ticks) {
            unsigned age=GARDEN_BOMB_BLAST_TICKS-g->ordnance.blast_ticks;
            if(age>=GARDEN_BOMB_BLAST_TICKS) age=GARDEN_BOMB_BLAST_TICKS-1U;
            unsigned frame=age*GARDEN_ASSET_LARGE_BLAST_COUNT/GARDEN_BOMB_BLAST_TICKS;
            n=emit(add,n,slots,g->ordnance.x,g->ordnance.y,
                    GARDEN_ASSET_LARGE_BLAST_BASE+frame,opacity(alpha_enabled,32U-age));
        } else n=emit(add,n,slots,g->ordnance.x,g->ordnance.y,
                     GARDEN_ASSET_LARGE_BOMB_BASE+(g->ordnance.age/2U)%GARDEN_ASSET_LARGE_BOMB_COUNT,32U);
    }
    for(unsigned pass=0U;pass<2U;pass++) for(unsigned i=0U;i<GARDEN_MAX_PICKUPS;i++) {
        const GardenPickup *p=&g->pickups[i];
        unsigned heal=p->kind==GARDEN_PICKUP_HEAL;
        if(!p->active || heal!=(pass==0U)) continue;
        n=emit(add,n,slots,p->x,p->y,heal ? GARDEN_ASSET_ORDNANCE_HEAL:GARDEN_ASSET_HIT_SPARK,32U);
    }
    if(g->reward.pickup_flash_ticks) {
        unsigned heal=g->reward.pickup_flash_kind==GARDEN_PICKUP_HEAL;
        unsigned age=16U-g->reward.pickup_flash_ticks;
        if(age>15U) age=15U;
        unsigned frame=age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/16U;
        n=emit(add,n,slots,g->reward.pickup_flash_x,g->reward.pickup_flash_y-(int32_t)age*1000,
            heal ? GARDEN_ASSET_ORDNANCE_HEAL:GARDEN_ASSET_ORDNANCE_HIT_BASE+frame,
            opacity(alpha_enabled,32U-age));
    }
    if(g->ordnance.active && g->ordnance.blast_ticks) {
        static const int8_t vx[8]={0,3,4,3,0,-3,-4,-3};
        static const int8_t vy[8]={-4,-3,0,3,4,3,0,-3};
        unsigned age=GARDEN_BOMB_BLAST_TICKS-g->ordnance.blast_ticks;
        if(age>=GARDEN_BOMB_BLAST_TICKS) age=GARDEN_BOMB_BLAST_TICKS-1U;
        unsigned frame=age*GARDEN_ASSET_ORDNANCE_HIT_COUNT/GARDEN_BOMB_BLAST_TICKS;
        unsigned bloom=age/2U+2U;
        if(bloom>=GARDEN_ASSET_ORDNANCE_BLAST_COUNT) bloom=GARDEN_ASSET_ORDNANCE_BLAST_COUNT-1U;
        /* Two offset shock blooms and eight flying slash fragments give the
         * real bomb a different scale from an ordinary impact spark. All
         * bounds come from atlas metadata in the normal dirty renderer. */
        for(unsigned i=0U;i<2U;i++) n=emit(add,n,slots,
            g->ordnance.x+(i ? 1:-1)*(int32_t)(24U+age*2U)*GARDEN_GRAIN,
            g->ordnance.y+(i ? 12:-12)*GARDEN_GRAIN,
            GARDEN_ASSET_ORDNANCE_BLAST_BASE+bloom,
            opacity(alpha_enabled,28U-age));
        for(unsigned i=0U;i<8U;i++) n=emit(add,n,slots,
            g->ordnance.x+(int32_t)vx[i]*(int32_t)(age+2U)*1800,
            g->ordnance.y+(int32_t)vy[i]*(int32_t)(age+2U)*1800,
            GARDEN_ASSET_ORDNANCE_HIT_BASE+frame,opacity(alpha_enabled,32U-age));
    }
    /* reserve and assembly share the same conditions, so n==slots whenever
     * capacity suffices. Fill a truncated reservation conservatively anyway. */
    if(n<slots) memmove(out+at+n,out+at+slots,(count-at)*sizeof(*out));
    return count+n;
}

void garden_ordnance_camera_apply(const GardenGame *g,GardenDraw *draws,unsigned count)
{
    static const int8_t sx[8]={1,-1,1,0,-1,1,0,-1};
    static const int8_t sy[8]={0,1,-1,1,0,-1,1,-1};
    if(!g || !draws || !g->ordnance.active || !g->ordnance.blast_ticks) return;
    unsigned age=GARDEN_BOMB_BLAST_TICKS-g->ordnance.blast_ticks;
    if(age>=10U) return;
    int amplitude=age<2U ? 6:age<5U ? 4:2;
    int dx=sx[age&7U]*amplitude,dy=sy[age&7U]*amplitude;
    if(count>GARDEN_DRAW_MAX) count=GARDEN_DRAW_MAX;
    for(unsigned i=0U;i<count;i++) {
        int x=draws[i].x+dx,y=draws[i].y+dy;
        draws[i].x=(int16_t)(x<-32768 ? -32768:x>32767 ? 32767:x);
        draws[i].y=(int16_t)(y<-32768 ? -32768:y>32767 ? 32767:y);
    }
}
