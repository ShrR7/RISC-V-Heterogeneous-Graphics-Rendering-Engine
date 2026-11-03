/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "frame_profile.h"
#include "garden_log.h"
#include <string.h>

static struct {
    uint64_t started, sum[FP_STAGE_COUNT], total;
    uint32_t hz, period, frames, steps, hud_frames, max, long_frames;
    unsigned active, window, verbose;
} profile;

void frame_profile_init(uint32_t timer_hz, uint32_t display_period_ticks)
{
    memset(&profile,0,sizeof(profile));
    profile.hz=timer_hz; profile.period=display_period_ticks;
}
unsigned frame_profile_active(void) { return profile.active; }

void frame_profile_start(uint64_t clock, unsigned verbose)
{
    memset(profile.sum,0,sizeof(profile.sum));
    profile.total=0; profile.frames=profile.steps=profile.hud_frames=0;
    profile.max=profile.long_frames=0;
    profile.verbose=verbose; profile.window++;
    /* The caller emits control messages before taking this boundary. */
    profile.started=clock; profile.active=1;
}
void frame_profile_record(const frame_profile_frame_t *frame, uint32_t elapsed)
{
    if (!profile.active) return;
    for (unsigned i=0;i<FP_STAGE_COUNT;i++) profile.sum[i]+=frame->ticks[i];
    profile.frames++; profile.steps+=frame->steps; profile.hud_frames+=frame->hud;
    profile.total+=elapsed;
    if (elapsed>profile.max) profile.max=elapsed;
    if (elapsed>(uint64_t)profile.period*3U/2U) profile.long_frames++;
}
static uint32_t average(uint64_t sum)
{
    return profile.frames ? (uint32_t)(sum*1000000ULL/profile.hz/profile.frames) : 0U;
}
void frame_profile_stop(uint64_t clock, unsigned cpu, unsigned alpha, uint32_t errors)
{
    uint64_t span=clock-profile.started, classified=0;
    if (!profile.active) return;
    profile.active=0;
    for (unsigned i=0;i<FP_STAGE_COUNT;i++) classified+=profile.sum[i];
    /* Complete span also includes the recorder and loop-boundary overhead. */
    uint64_t other=span>classified ? span-classified : 0;
    /* One report AFTER the window closes. No per-frame print or floating point.
     * Raw elapsed ticks are hex to avoid the BSP formatter's signed %d. */
    garden_log_printf("[FRAME_PROFILE] window=%d verbose=%d frames=%d steps=%d span_ticks=0x%x span_hi=0x%x span_ms=%d timer_hz=%d fps10=%d input_avg_us=%d logic_avg_us=%d list_avg_us=%d scene_avg_us=%d hud_avg_us=%d hud_frames=%d gpu_idle_avg_us=%d swap_avg_us=%d dbg_avg_us=%d log_avg_us=%d other_avg_us=%d total_avg_us=%d total_max_us=%d long_frames=%d errors=%d alpha=%d backend=%s\r\n",
        profile.window,profile.verbose,profile.frames,profile.steps,
        (uint32_t)span,(uint32_t)(span>>32),(uint32_t)(span*1000ULL/profile.hz),profile.hz,
        span ? (uint32_t)((uint64_t)profile.frames*profile.hz*10U/span) : 0,
        average(profile.sum[FP_INPUT]),average(profile.sum[FP_LOGIC]),
        average(profile.sum[FP_LIST]),average(profile.sum[FP_SCENE]),
        average(profile.sum[FP_HUD]),profile.hud_frames,
        average(profile.sum[FP_GPU_IDLE]),average(profile.sum[FP_SWAP]),
        average(profile.sum[FP_DEBUG]),average(profile.sum[FP_LOG]),average(other),
        average(span),(uint32_t)((uint64_t)profile.max*1000000ULL/profile.hz),
        profile.long_frames,errors,alpha,cpu ? "CPU":"GPU");
}
