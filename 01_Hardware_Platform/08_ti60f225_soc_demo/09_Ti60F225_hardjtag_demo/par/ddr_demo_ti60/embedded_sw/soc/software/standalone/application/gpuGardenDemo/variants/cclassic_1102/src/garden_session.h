/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "garden_core.h"
typedef struct {
    uint32_t last_activity_ms,activity_serial;
    uint8_t mode,restart_sent;
} GardenSession;
void garden_session_reset(GardenSession *s,const GardenGame *g,uint32_t now_ms,
                          uint32_t activity_serial);
/* Physical activity only, never neutral protocol heartbeat time. Millisecond
 * subtraction supports uint32 clock wrap and never alters pause state. */
uint16_t garden_session_update(GardenSession *s,GardenGame *g,uint32_t now_ms,
                               uint32_t activity_serial,uint32_t activity_ms);
