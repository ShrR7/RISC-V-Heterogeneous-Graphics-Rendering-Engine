/* SPDX-License-Identifier: GPL-3.0-or-later
 * Capability and pixel readback test for PR #32, entirely in 2 KiB scratch.
 * A register-less Stage 6/7 board exits before any CTRL=0x105 command.
 */
#include "gpu_alpha_verify.h"
#include "gpu_alpha_ref.h"
#include "gpu_driver.h"
#include "gpu_regs.h"
#include "vexriscv.h"
#include <string.h>

#define SLOT_BYTES 1024U
#define SLOT_WORDS (SLOT_BYTES / 2U)
#define SOURCE_SLOT GPU_ALPHA_VERIFY_BASE
#define DEST_SLOT (GPU_ALPHA_VERIFY_BASE + SLOT_BYTES)
#define HEADER_BYTES 32U
#define PROBE_KEY 0xF81FU

typedef struct {
    uint16_t width, height, src_stride, dst_stride;
    uint8_t src_offset, dst_offset;
} Geometry;

/* Odd pixel strides, independent non-16-byte-aligned addresses, several
 * burst-lane boundaries and multiple rows in every case. */
static const Geometry geometries[] = {
    { 7U, 3U, 18U, 22U, 2U, 14U },
    { 63U, 4U, 130U, 134U, 6U, 2U },
    { 65U, 5U, 134U, 138U, 14U, 6U },
    { 9U, 3U, 22U, 26U, 0U, 14U },
    { 1U, 3U, 6U, 10U, 14U, 2U }
};
static const uint8_t alphas[] = { 0U, 1U, 15U, 16U, 17U, 31U, 32U };
static const uint16_t palette[] = {
    0x0000U, 0xFFFFU, 0xF800U, 0x07E0U, 0x001FU, 0x1234U,
    0xABCDU, 0x0841U, 0x18C3U, 0xF7BEU, 0x0410U, 0x8020U
};
static GpuAlphaVerifyStats stats;

static uint16_t guard_word(unsigned index, unsigned dest)
{
    return (uint16_t)((index * 4051U + (dest ? 0x52A5U : 0xA53CU)) & 0xFFFFU);
}

static uint16_t source_pixel(unsigned x, unsigned y)
{
    if ((x + 2U * y) % 5U == 0U) return PROBE_KEY;
    return palette[(x + 3U * y) % (sizeof(palette) / sizeof(palette[0]))];
}

/* The volatile stores and cache publication match the existing board probe.
 * GPU writes occur behind the CPU cache; invalidate before all readbacks. */
static void publish_cpu(void)
{
    asm volatile("fence rw,rw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence rw,rw" ::: "memory");
}

static int pixel_at_word(unsigned word, const Geometry *g, int dest,
                         unsigned *x, unsigned *y)
{
    unsigned start = (HEADER_BYTES + (dest ? g->dst_offset : g->src_offset)) / 2U;
    unsigned stride = (dest ? g->dst_stride : g->src_stride) / 2U;
    if (word < start) return 0;
    unsigned relative = word - start;
    *y = relative / stride;
    *x = relative % stride;
    return *y < g->height && *x < g->width;
}

static void mismatch(uint32_t address, uint16_t expected, uint16_t actual,
                     uint32_t *counter)
{
    if (!stats.pixel_errors && !stats.source_errors && !stats.guard_errors) {
        stats.first_addr = address;
        stats.first_expected = expected;
        stats.first_actual = actual;
    }
    ++*counter;
}

static void initialize_slots(const Geometry *g)
{
    volatile uint16_t *src = (volatile uint16_t *)(uintptr_t)SOURCE_SLOT;
    volatile uint16_t *dst = (volatile uint16_t *)(uintptr_t)DEST_SLOT;
    for (unsigned i = 0U; i < SLOT_WORDS; ++i) {
        unsigned x, y;
        src[i] = pixel_at_word(i, g, 0, &x, &y) ? source_pixel(x, y) : guard_word(i, 0U);
        dst[i] = guard_word(i, 1U);
    }
    publish_cpu();
}

static void verify_slots(const Geometry *g, unsigned alpha)
{
    const volatile uint16_t *src = (const volatile uint16_t *)(uintptr_t)SOURCE_SLOT;
    const volatile uint16_t *dst = (const volatile uint16_t *)(uintptr_t)DEST_SLOT;
    publish_cpu();
    for (unsigned i = 0U; i < SLOT_WORDS; ++i) {
        unsigned x, y;
        uint16_t expected = pixel_at_word(i, g, 0, &x, &y) ? source_pixel(x, y) : guard_word(i, 0U);
        uint16_t actual = src[i];
        if (actual != expected) mismatch(SOURCE_SLOT + 2U * i, expected, actual, &stats.source_errors);
        expected = guard_word(i, 1U);
        int pixel = pixel_at_word(i, g, 1, &x, &y);
        if (pixel) {
            uint16_t input = source_pixel(x, y);
            expected = gpu_alpha_key_rgb565(input, expected, PROBE_KEY, alpha);
            ++stats.pixels;
            if (input == PROBE_KEY) ++stats.key_pixels;
        }
        actual = dst[i];
        if (actual != expected) {
            mismatch(DEST_SLOT + 2U * i, expected, actual,
                     pixel ? &stats.pixel_errors : &stats.guard_errors);
        }
    }
}

int gpu_alpha_verify(void)
{
    int rc;
    memset(&stats, 0, sizeof(stats));
    rc = gpu_wait_idle(3000000U);
    if (rc != GPU_OK) {
        stats.exit_status = GPU_STATUS;
        stats.restore_skipped = 1U;
        return rc;
    }
    uint32_t saved = GPU_ALPHA;
    stats.saved_alpha = saved;
    GPU_ALPHA = 16U;
    uint32_t observed = GPU_ALPHA;
    if (observed != 16U) {
        /* Legacy register files return 0 and ignore the write. Crucially,
         * no Alpha command has been dispatched on this path. */
        rc = observed == 0U ? GPU_ESTATE : GPU_EHW;
        goto restore;
    }
    stats.register_present = 1U;
    GPU_ALPHA = 33U;
    if (GPU_ALPHA != 32U) { rc = GPU_EHW; goto restore; }
    GPU_ALPHA = 0U;
    if (GPU_ALPHA != 0U) { rc = GPU_EHW; goto restore; }
    GPU_ALPHA = 32U;
    if (GPU_ALPHA != 32U) { rc = GPU_EHW; goto restore; }

    for (unsigned a = 0U; a < sizeof(alphas) / sizeof(alphas[0]); ++a) {
        for (unsigned c = 0U; c < sizeof(geometries) / sizeof(geometries[0]); ++c) {
            const Geometry *g = &geometries[c];
            uint32_t src = SOURCE_SLOT + HEADER_BYTES + g->src_offset;
            uint32_t dst = DEST_SLOT + HEADER_BYTES + g->dst_offset;
            initialize_slots(g);
            ++stats.commands;
            rc = gpu_alpha_bitblt(src, dst, g->width, g->height,
                                   g->src_stride, g->dst_stride, PROBE_KEY, alphas[a]);
            if (rc != GPU_OK) goto restore;
            ++stats.cases;
            verify_slots(g, alphas[a]);
            if (stats.pixel_errors || stats.source_errors || stats.guard_errors) {
                rc = GPU_EHW;
                goto restore;
            }
        }
    }
    rc = GPU_OK;
restore:
    stats.exit_status = GPU_STATUS;
    if ((stats.exit_status & STATUS_BUSY) != 0U) {
        /* The engine consumes Alpha as a live value. Even on timeout/error,
         * restoring now could alter an unfinished operation's pixels. */
        stats.restore_skipped = 1U;
        if (rc == GPU_OK) rc = GPU_ESTATE;
    } else {
        GPU_ALPHA = saved;
    }
    return rc;
}

const GpuAlphaVerifyStats *gpu_alpha_verify_stats(void) { return &stats; }
