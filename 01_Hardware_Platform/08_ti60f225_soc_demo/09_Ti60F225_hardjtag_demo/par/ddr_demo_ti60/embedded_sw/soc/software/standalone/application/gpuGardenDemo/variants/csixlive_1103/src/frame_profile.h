/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

enum frame_profile_stage {
    FP_INPUT, FP_LOGIC, FP_LIST, FP_SCENE, FP_HUD,
    FP_GPU_IDLE, FP_SWAP, FP_DEBUG, FP_LOG, FP_STAGE_COUNT
};
typedef struct {
    uint32_t ticks[FP_STAGE_COUNT];
    unsigned steps, hud;
} frame_profile_frame_t;

void frame_profile_init(uint32_t timer_hz, uint32_t display_period_ticks);
unsigned frame_profile_active(void);
void frame_profile_start(uint64_t clock, unsigned verbose);
void frame_profile_record(const frame_profile_frame_t *frame, uint32_t elapsed);
void frame_profile_stop(uint64_t clock, unsigned cpu, unsigned alpha, uint32_t errors);
