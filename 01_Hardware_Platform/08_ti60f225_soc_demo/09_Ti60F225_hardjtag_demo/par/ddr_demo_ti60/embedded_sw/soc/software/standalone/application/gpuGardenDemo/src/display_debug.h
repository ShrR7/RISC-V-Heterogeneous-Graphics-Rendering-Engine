/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

/* Read-only observer. Do not write DBG_CTRL: this delivery also restarts
 * its top-level prewarm FSM on clear_all. Never access unaligned 0x5e. */
unsigned display_debug_init(void);
void display_debug_sample(void);
void display_debug_report(const char *phase, uint32_t frame);
