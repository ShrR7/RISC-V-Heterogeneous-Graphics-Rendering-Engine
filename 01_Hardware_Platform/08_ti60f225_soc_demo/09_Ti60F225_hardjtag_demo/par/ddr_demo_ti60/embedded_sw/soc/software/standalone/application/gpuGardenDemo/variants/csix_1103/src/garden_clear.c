/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_clear.h"
#include <string.h>

static GardenClearYield yield_hook;
static GardenClearStats stats;

static uint32_t add_sat(uint32_t value,uint32_t amount)
{
    return UINT32_MAX-value<amount ? UINT32_MAX:value+amount;
}

void garden_clear_set_yield(GardenClearYield yield)
{
    yield_hook=yield;
}

void garden_clear_zero(void *destination,size_t bytes)
{
    unsigned char *out=(unsigned char*)destination;
    stats.calls=add_sat(stats.calls,1U);
    while(bytes){
        size_t chunk=bytes<GARDEN_CLEAR_CHUNK_BYTES ? bytes:GARDEN_CLEAR_CHUNK_BYTES;
        memset(out,0,chunk);
        stats.bytes=add_sat(stats.bytes,(uint32_t)chunk);
        stats.chunks=add_sat(stats.chunks,1U);
        if(stats.max_chunk<chunk)stats.max_chunk=(uint32_t)chunk;
        out+=chunk;bytes-=chunk;
        if(bytes && yield_hook){
            stats.yields=add_sat(stats.yields,1U);
            yield_hook();
        }
    }
}

const GardenClearStats *garden_clear_get_stats(void)
{
    return &stats;
}

void garden_clear_reset_stats(void)
{
    memset(&stats,0,sizeof stats);
}
