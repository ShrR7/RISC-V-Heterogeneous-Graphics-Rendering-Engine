/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t register_present;
    uint32_t cases, sprites, commands, pixels, key_pixels;
    uint32_t high_region_sources, opaque_cases, alpha_cases;
    uint32_t pixel_errors, source_errors, guard_errors;
    uint32_t first_source, last_source, min_source, max_source;
    uint32_t first_addr;
    uint16_t first_expected, first_actual;
    uint32_t saved_alpha, restore_skipped, exit_status;
} GpuPrecisionVerifyStats;

/* Test two real appended fine-angle sprites per type/colour family, using
 * ordinary keyed blits and keyed Alpha16. Every selected source is beyond
 * the old asset-window end; each patch includes both key and visible pixels.
 * Writes are confined to the asset window's final 2 KiB scratch, never the
 * atlas, panel source, framebuffers, or display/debug control registers.
 * A missing family or pixel/guard mismatch is a failure, not a skipped case.
 * Run with no concurrent drawer after gpu_alpha_verify() succeeds. On an
 * idle return ALPHA is restored and CTRL is lowered. If an error leaves the
 * engine busy, its live parameters are preserved, restore_skipped is set,
 * and the caller must stop rather than render over the unfinished command.
 */
int gpu_precision_verify(void);
const GpuPrecisionVerifyStats *gpu_precision_verify_stats(void);
