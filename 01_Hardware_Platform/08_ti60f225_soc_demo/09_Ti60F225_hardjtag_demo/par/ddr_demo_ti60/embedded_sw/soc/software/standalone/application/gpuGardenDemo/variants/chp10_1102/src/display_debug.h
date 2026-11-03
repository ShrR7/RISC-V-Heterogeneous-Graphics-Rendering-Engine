/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

/* Read-clear counters are accumulated; no DBG_CTRL writes or 0x5e access. */
typedef struct {
    uint64_t px,lines,full,stall_ar,stall_r;
    uint32_t samples,saturation,status;
    uint32_t saturated_samples[5];
} DisplayDebugTotals;
unsigned display_debug_init(void);
void display_debug_sample(void);
void display_debug_report(const char *phase, uint32_t frame);
const DisplayDebugTotals *display_debug_totals(void);
