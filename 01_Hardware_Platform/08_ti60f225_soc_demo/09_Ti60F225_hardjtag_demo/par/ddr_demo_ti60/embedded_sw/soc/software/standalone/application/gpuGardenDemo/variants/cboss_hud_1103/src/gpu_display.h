#pragma once

#include <stdint.h>

/*
 * Stage 6 dual-buffer display API.
 *
 * Contract (interface_spec.md §7):
 *   - front buffer = FB_STATUS.fb_sel_active -> display reads it, CPU/GPU must
 *     never write it;
 *   - back buffer  = the other one -> the only legal render target;
 *   - the display side applies a swap request at its own safe point, so the
 *     software polls FB_STATUS until fb_sel_active matches the request.
 */

#define FB_SEL_FB0 0U
#define FB_SEL_FB1 1U

typedef struct {
    uint32_t swaps;        /* completed swaps */
    uint32_t timeouts;     /* display_wait_swap() timeouts */
    uint32_t req_writes;   /* FB_CTRL writes */
    uint32_t noop_writes;  /* requests equal to the active buffer */
    uint32_t max_polls;    /* worst-case poll count for one swap */
    uint32_t front_rejects;/* attempts to render into the front buffer */
} display_stats_t;

typedef struct {
    uint32_t gpu_idle_ticks;
    uint32_t swap_wait_ticks;
} display_frame_timing_t;
const display_frame_timing_t *display_frame_timing(void);

/* Read FB_STATUS. */
uint32_t display_get_active(void);
uint32_t display_swap_pending(void);

/* Base address of a buffer index. */
uint32_t display_addr_of(uint32_t sel);

/* Cache the power-on active buffer; call once after bsp_init(). */
int display_init(void);

/*
 * Confirm that the bitstream really implements FB_CTRL/FB_STATUS.
 *
 * A Stage 5 bitstream has no 0x28/0x2C registers, so a naive swap loop would
 * spin in timeout 10,000 times.  This probe writes the opposite request and
 * checks that either swap_pending rises at once (the two bits must differ) or
 * the display applies the request within a couple of frames.  Returns GPU_OK
 * when the handshake is alive, GPU_EHW otherwise.
 */
int display_probe(void);

/* Write FB_CTRL.fb_sel_req (only bit0 is meaningful). */
int display_request_fb(uint32_t sel);

/* Poll until FB_STATUS.fb_sel_active == sel.  Never waits forever. */
int display_wait_swap(uint32_t sel, uint32_t timeout);

/* Frame lifecycle.  render_begin() locks the back buffer for this frame. */
void render_begin(void);
int render_end(uint32_t timeout);

/* Geometry of the frame currently being rendered. */
uint32_t display_back_sel(void);
uint32_t display_back_addr(void);
uint32_t display_front_sel(void);
uint32_t display_front_addr(void);

/*
 * Returns 1 when `addr` lies inside the buffer the display is reading now.
 * Game code must not write to any address that satisfies this.
 */
int display_is_front(uint32_t addr);

/* Guard used by every render helper; counts violations. */
int display_check_render_target(uint32_t addr);

const display_stats_t *display_stats(void);
void display_stats_reset(void);
