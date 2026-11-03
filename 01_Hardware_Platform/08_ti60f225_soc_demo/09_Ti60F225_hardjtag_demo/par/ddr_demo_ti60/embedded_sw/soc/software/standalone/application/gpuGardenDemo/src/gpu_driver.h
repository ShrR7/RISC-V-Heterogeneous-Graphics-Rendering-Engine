#pragma once

#include <stdint.h>

enum gpu_result {
    GPU_OK       = 0,
    GPU_EINVAL   = -1,
    GPU_EOVERLAP = -2,
    GPU_ETIMEOUT = -3,
    GPU_EHW      = -4,
    GPU_ESTATE   = -5
};

/* Fill a rectangle with a solid RGB565 colour (op=00). */
int gpu_fill(uint32_t dst, uint32_t width, uint32_t height,
             uint32_t dst_stride, uint16_t color);

/* Copy a rectangle (op=01).  Source and destination must not overlap. */
int gpu_copy(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
             uint32_t src_stride, uint32_t dst_stride);

/* Color Key BitBlt (op=10): source pixels equal to key are not written. */
int gpu_bitblt(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
               uint32_t src_stride, uint32_t dst_stride, uint16_t key);

/* PR #32 Alpha + Color Key BitBlt (CTRL=0x105), alpha in 0..32.
 * Equal-to-key source pixels leave destination unchanged. Non-key pixels
 * blend each RGB565 channel with rounding (+16 before >>5). Unlike the
 * hardware clamp, this API rejects alpha >32. Both source and destination
 * read rows, including their 16-byte alignment offsets, must fit in 4 KiB.
 * Call only after gpu_alpha_verify() reports GPU_OK on the loaded fabric.
 */
int gpu_alpha_bitblt(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
                     uint32_t src_stride, uint32_t dst_stride,
                     uint16_t key, uint32_t alpha);

/* Last status word and poll count of the most recent accepted operation. */
uint32_t gpu_last_status(void);
uint32_t gpu_last_polls(void);

/* 1 while an engine reports busy. */
int gpu_busy(void);

/* Wait for the engine to leave busy; returns GPU_OK or GPU_ETIMEOUT. */
int gpu_wait_idle(uint32_t timeout);
