/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_heading_precision.h"

/* round(tan((2k+1)*pi/32)*2^30): 5.625,16.875,28.125,39.375deg. */
static const uint32_t tangent32_q30[4]={105754339U,325716021U,573926962U,881197142U};
/* round(tan((2k+1)*pi/64)*2^30): 2.8125..42.1875deg in5.625deg steps. */
static const uint32_t tangent64_q30[8]={52749553U,159274554U,268958326U,384190968U,
    507842061U,643576082U,796341210U,973182862U};

static uint32_t magnitude32(int32_t v)
{ return v<0 ? 0U-(uint32_t)v:(uint32_t)v; } /* INT32_MIN safely becomes2^31. */

static uint16_t nearest_heading(int32_t vx,int32_t vy,uint32_t fallback,
                                const uint32_t *thresholds,unsigned count,unsigned step)
{
    uint32_t ax=magnitude32(vx),ay=magnitude32(vy);
    if (!(ax|ay)) return (uint16_t)(((fallback+(step>>1))&1023U)&~(step-1U));
    uint32_t minor=ax<ay ? ax:ay,major=ax<ay ? ay:ax;
    uint64_t scaled=(uint64_t)minor<<30;
    /* Upper-bound search retains the original inclusive lower threshold:
     * ties advance to the next bin, exactly as the old linear scan. */
    unsigned bin=0U,end=count;
    while(bin<end) {
        unsigned mid=(bin+end)>>1;
        if(scaled>=(uint64_t)major*thresholds[mid]) bin=mid+1U;
        else end=mid;
    }
    /* Both operands fit <=2^61: |v|<=2^31,threshold<2^30. No rescaling
     * or truncation of small components is needed, even at INT32 extremes. */
    unsigned angle=bin*step;
    if (ay>ax) angle=256U-angle;
    if (vx<0) angle=512U-angle;
    if (vy<0) angle=(1024U-angle)&1023U;
    return (uint16_t)angle;
}

uint16_t heading32_from_velocity(int32_t vx,int32_t vy,uint32_t fallback)
{ return nearest_heading(vx,vy,fallback,tangent32_q30,4U,32U); }
uint16_t heading64_from_velocity(int32_t vx,int32_t vy,uint32_t fallback)
{ return nearest_heading(vx,vy,fallback,tangent64_q30,8U,16U); }

/* Validate an already decoded exact64-direction candidate in at most two
 * comparisons. Undo the same reflections as nearest_heading, then test its
 * inclusive lower/exclusive upper Q30 boundaries. No heading is recomputed. */
unsigned heading64_matches(int32_t vx,int32_t vy,uint32_t fallback,unsigned angle)
{
    if(angle>1008U || (angle&15U)) return 0U;
    uint32_t ax=magnitude32(vx),ay=magnitude32(vy);
    if(!(ax|ay)) return angle==(((fallback+8U)&1023U)&~15U);
    if(vy<0) angle=(1024U-angle)&1023U;
    if(vx<0) angle=(512U-angle)&1023U;
    if(ay>ax) angle=(256U-angle)&1023U;
    if(angle>128U) return 0U;
    unsigned bin=angle>>4;
    uint32_t minor=ax<ay ? ax:ay,major=ax<ay ? ay:ax;
    uint64_t scaled=(uint64_t)minor<<30;
    if(bin && scaled<(uint64_t)major*tangent64_q30[bin-1U]) return 0U;
    return bin==8U || scaled<(uint64_t)major*tangent64_q30[bin];
}
