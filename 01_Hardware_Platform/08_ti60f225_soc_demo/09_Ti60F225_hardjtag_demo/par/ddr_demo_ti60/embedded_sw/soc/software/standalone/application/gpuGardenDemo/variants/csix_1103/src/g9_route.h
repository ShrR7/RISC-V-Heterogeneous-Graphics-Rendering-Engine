/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef G9_ROUTE_H
#define G9_ROUTE_H
#include "garden_render.h"
typedef struct {
    uint32_t frames,enemies,migrated,legacy,descriptors,tiles;
    uint32_t fallback_alpha,fallback_region,fallback_occlusion,fallback_tiles,fallback_desc;
    uint32_t atlas_addr,desc_addr,errors;
} G9RouteStats;
int g9_route_init(void);
int g9_route_prepare(const GardenDraw *draws,unsigned count,unsigned enabled);
unsigned g9_route_skip(unsigned index);
void g9_route_clear(void);
const G9RouteStats *g9_route_stats(void);
#endif
