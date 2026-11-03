/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

/* Reserved final 2 KiB of the asset window. These tests never touch an atlas
 * pixel, framebuffer, or display-swap register. Panel source ends here. */
#define GPU_ALPHA_VERIFY_BASE 0x005FF800U
#define GPU_ALPHA_VERIFY_END  0x00600000U

typedef struct {
    uint32_t register_present;
    uint32_t cases, commands, pixels, key_pixels;
    uint32_t pixel_errors, source_errors, guard_errors;
    uint32_t first_addr;
    uint16_t first_expected, first_actual;
    uint32_t saved_alpha;
    uint32_t restore_skipped, exit_status;
} GpuAlphaVerifyStats;

/* GPU_OK: register and keyed Alpha pixels passed.
 * GPU_ESTATE: 0x30 is absent (write 16, read 0); no Alpha command was sent.
 * GPU_EHW: register behavior or pixel/source/guard check failed.
 * Other negative values: driver geometry, engine error, or timeout.
 * Save/restore ALPHA on idle return paths. If an error leaves GPU busy,
 * preserve the running command's live ALPHA; restore_skipped records this
 * exception and the caller must stop rendering. Run with no other drawer.
 */
int gpu_alpha_verify(void);
const GpuAlphaVerifyStats *gpu_alpha_verify_stats(void);
