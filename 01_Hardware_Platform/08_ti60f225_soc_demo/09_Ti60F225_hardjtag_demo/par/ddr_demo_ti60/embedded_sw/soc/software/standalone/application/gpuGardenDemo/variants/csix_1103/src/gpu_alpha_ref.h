/* SPDX-License-Identifier: GPL-3.0-or-later
 * PR #32 RGB565 Alpha reference. Pure integer code; no MMIO or cache state.
 */
#pragma once
#include <stdint.h>

/* Reference semantics are defined only for alpha in 0..32. The driver
 * validates this range; do not infer behavior of illegal register writes. */
static inline uint16_t gpu_alpha_rgb565(uint16_t src, uint16_t dst,
                                       uint32_t alpha)
{
    uint32_t a = alpha;
    uint32_t b = 32U - a;
    uint32_t r = (((uint32_t)(src >> 11) * a +
                   (uint32_t)(dst >> 11) * b + 16U) >> 5);
    uint32_t g = (((uint32_t)((src >> 5) & 63U) * a +
                   (uint32_t)((dst >> 5) & 63U) * b + 16U) >> 5);
    uint32_t blue = (((uint32_t)(src & 31U) * a +
                      (uint32_t)(dst & 31U) * b + 16U) >> 5);
    return (uint16_t)((r << 11) | (g << 5) | blue);
}

static inline uint16_t gpu_alpha_key_rgb565(uint16_t src, uint16_t dst,
                                           uint16_t key, uint32_t alpha)
{
    return src == key ? dst : gpu_alpha_rgb565(src, dst, alpha);
}
