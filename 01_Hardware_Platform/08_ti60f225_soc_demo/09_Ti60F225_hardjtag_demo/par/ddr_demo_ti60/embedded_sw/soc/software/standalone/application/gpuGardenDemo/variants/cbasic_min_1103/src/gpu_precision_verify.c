/* SPDX-License-Identifier: GPL-3.0-or-later
 * Physical readback of new fine-heading sprite sources. No driver draw
 * target is widened: raw commands use only the reserved probe destination.
 */
#include "gpu_precision_verify.h"
#include "gpu_alpha_ref.h"
#include "gpu_driver.h"
#include "gpu_regs.h"
#include "garden_assets.h"
#include "garden_core.h"
#ifndef GPU_PRECISION_VERIFY_HOST_TEST
#include "vexriscv.h"
#endif
#include <string.h>

#define PROBE_BYTES 2048U
#define PROBE_WORDS (PROBE_BYTES / 2U)
#define PROBE_BASE (ASSET_BASE_ADDR + ASSET_SIZE_BYTES - PROBE_BYTES)
/* This fixed historical size is a coverage boundary, not a new window end. */
#define OLD_WINDOW_END (ASSET_BASE_ADDR + 0x0003C000U)
#define POLL_MAX 3000000U
#define PATCH_EDGE 8U
#define PATCH_PIXELS (PATCH_EDGE * PATCH_EDGE)
#define HEADER_BYTES 32U

typedef struct {
    uint32_t src;
    uint16_t width, height, stride;
    uint16_t pixels[PATCH_PIXELS];
} Patch;

static GpuPrecisionVerifyStats stats;

#ifdef GPU_PRECISION_VERIFY_HOST_TEST
/* Only the host contract replaces cache publication and executes the raw
 * command after the exact same register writes. These hooks do not exist
 * in the firmware build. */
extern void gpu_precision_verify_host_publish(void);
extern void gpu_precision_verify_host_dispatch(void);
#endif

static void publish_cpu(void)
{
#ifdef GPU_PRECISION_VERIFY_HOST_TEST
    gpu_precision_verify_host_publish();
#else
    asm volatile("fence rw,rw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence rw,rw" ::: "memory");
#endif
}

static int wait_idle(void)
{
    for (uint32_t polls = 0U; polls < POLL_MAX; ++polls)
        if ((GPU_STATUS & STATUS_BUSY) == 0U) return GPU_OK;
    return GPU_ETIMEOUT;
}

static int layout_valid(void)
{
    uint64_t end = (uint64_t)ASSET_BASE_ADDR + ASSET_SIZE_BYTES;
    return GARDEN_ATLAS_ADDRESS == ASSET_BASE_ADDR &&
           ASSET_SIZE_BYTES >= 8192U && end <= 0x100000000ULL &&
           GARDEN_ATLAS_BYTES <= ASSET_SIZE_BYTES - 8192U &&
           OLD_WINDOW_END < PROBE_BASE;
}

static int sprite_valid(const garden_sprite_t *s)
{
    if (!s || !s->width || !s->height || (s->offset & 1U) ||
        (garden_sprite_stride(s) & 1U) || garden_sprite_stride(s) < 2U * s->width) return 0;
    uint64_t end = (uint64_t)s->offset +
                   (uint64_t)(s->height - 1U) * garden_sprite_stride(s) + 2U * s->width;
    return end <= GARDEN_ATLAS_BYTES &&
           (uint64_t)GARDEN_ATLAS_ADDRESS + s->offset >= OLD_WINDOW_END;
}

/* Find a real mixed key/non-key rectangle. Empty corners are not a valid
 * test of blending; an all-visible rectangle is not a valid ColorKey test.
 * The metadata bound is checked before any source memory is dereferenced.
 */
static int select_patch(unsigned id, Patch *patch)
{
    if (id < GARDEN_ASSET_PRECISION_BASE || id >= GARDEN_ASSET_COUNT)
        return 0;
    const garden_sprite_t *s = garden_asset(id);
    if (!sprite_valid(s)) return 0;
    unsigned width = s->width < PATCH_EDGE ? s->width : PATCH_EDGE;
    unsigned height = s->height < PATCH_EDGE ? s->height : PATCH_EDGE;
    for (unsigned y0 = 0U; y0 + height <= s->height; ++y0) {
        for (unsigned x0 = 0U; x0 + width <= s->width; ++x0) {
            uint32_t src = GARDEN_ATLAS_ADDRESS + s->offset +
                           y0 * garden_sprite_stride(s) + 2U * x0;
            unsigned keys = 0U, visible = 0U;
            for (unsigned y = 0U; y < height; ++y) {
                const volatile uint16_t *row =
                    (const volatile uint16_t *)(uintptr_t)(src + y * garden_sprite_stride(s));
                for (unsigned x = 0U; x < width; ++x) {
                    uint16_t pixel = row[x];
                    patch->pixels[y * width + x] = pixel;
                    keys += pixel == GARDEN_KEY_COLOR;
                    visible += pixel != GARDEN_KEY_COLOR;
                }
            }
            if (keys && visible) {
                patch->src = src;
                patch->width = (uint16_t)width;
                patch->height = (uint16_t)height;
                patch->stride = garden_sprite_stride(s);
                return 1;
            }
        }
    }
    return 0;
}

static uint16_t guard_word(unsigned word, unsigned case_number)
{
    return (uint16_t)(word * 4051U + 0x52A5U + case_number * 7919U);
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

static int dispatch(const Patch *p, uint32_t dst, uint32_t dst_stride,
                    unsigned alpha)
{
    /* Register parameters, including ALPHA, are live to an active engine. */
    int rc = wait_idle();
    if (rc != GPU_OK) return rc;
    GPU_SRC_ADDR = p->src;
    GPU_DST_ADDR = dst;
    GPU_WIDTH = p->width;
    GPU_HEIGHT = p->height;
    GPU_SRC_STRIDE = p->stride;
    GPU_DST_STRIDE = dst_stride;
    GPU_COLOR = GARDEN_KEY_COLOR;
    GPU_ALPHA = alpha;
    if (GPU_ALPHA != alpha) return GPU_EHW;
    GPU_CTRL = 0U;
    GPU_CTRL = alpha == 32U ? GPU_CTRL_BITBLT : GPU_CTRL_ALPHA_BITBLT;
    ++stats.commands;
#ifdef GPU_PRECISION_VERIFY_HOST_TEST
    gpu_precision_verify_host_dispatch();
#endif
    for (uint32_t polls = 0U; polls < POLL_MAX; ++polls) {
        uint32_t status = GPU_STATUS;
        if (status & STATUS_ERROR) return GPU_EHW;
        if ((status & STATUS_DONE) && !(status & STATUS_BUSY)) return GPU_OK;
    }
    return GPU_ETIMEOUT;
}

static int verify_case(const Patch *p, unsigned alpha)
{
    int rc = wait_idle();
    if (rc != GPU_OK) return rc;
    unsigned case_number = stats.cases;
    /* Vary halfword alignment and use odd pixel strides. The entire scratch
     * is checked, including leading/trailing words and each row's padding. */
    unsigned start_word = (HEADER_BYTES + 2U + 2U * (case_number & 7U)) / 2U;
    unsigned stride_words = p->width + 3U + (case_number & 1U);
    if (start_word + (p->height - 1U) * stride_words + p->width > PROBE_WORDS)
        return GPU_EINVAL;
    volatile uint16_t *dst = (volatile uint16_t *)(uintptr_t)PROBE_BASE;
    for (unsigned i = 0U; i < PROBE_WORDS; ++i) dst[i] = guard_word(i, case_number);
    publish_cpu();
    rc = dispatch(p, PROBE_BASE + 2U * start_word, 2U * stride_words, alpha);
    if (rc != GPU_OK) return rc;
    ++stats.cases;
    if (alpha == 32U) ++stats.opaque_cases;
    else ++stats.alpha_cases;
    publish_cpu();
    for (unsigned i = 0U; i < PROBE_WORDS; ++i) {
        uint16_t expected = guard_word(i, case_number);
        unsigned x = 0U, y = 0U;
        int pixel = 0;
        if (i >= start_word) {
            y = (i - start_word) / stride_words;
            x = (i - start_word) % stride_words;
            pixel = y < p->height && x < p->width;
        }
        if (pixel) {
            uint16_t src = p->pixels[y * p->width + x];
            expected = gpu_alpha_key_rgb565(src, expected, GARDEN_KEY_COLOR, alpha);
            ++stats.pixels;
            stats.key_pixels += src == GARDEN_KEY_COLOR;
        }
        uint16_t actual = dst[i];
        if (actual != expected)
            mismatch(PROBE_BASE + 2U * i, expected, actual,
                     pixel ? &stats.pixel_errors : &stats.guard_errors);
    }
    /* The GPU must not write a source. The parent additionally checks the
     * complete atlas CRC after all probes, covering unselected pixels too. */
    for (unsigned y = 0U; y < p->height; ++y) {
        const volatile uint16_t *src =
            (const volatile uint16_t *)(uintptr_t)(p->src + y * p->stride);
        for (unsigned x = 0U; x < p->width; ++x) {
            uint16_t expected = p->pixels[y * p->width + x];
            if (src[x] != expected)
                mismatch(p->src + y * p->stride + 2U * x, expected, src[x],
                         &stats.source_errors);
        }
    }
    return stats.pixel_errors || stats.source_errors || stats.guard_errors ? GPU_EHW : GPU_OK;
}

int gpu_precision_verify(void)
{
    memset(&stats, 0, sizeof(stats));
    if (!layout_valid()) return GPU_EINVAL;
    if (GPU_ID != GPU_MAGIC_ID) return GPU_ESTATE;
    int rc = wait_idle();
    if (rc != GPU_OK) {
        stats.exit_status = GPU_STATUS;
        stats.restore_skipped = 1U;
        return rc;
    }
    uint32_t saved = GPU_ALPHA;
    stats.saved_alpha = saved;
    GPU_ALPHA = 16U;
    if (GPU_ALPHA != 16U) { rc = GPU_ESTATE; goto restore; }
    stats.register_present = 1U;
    publish_cpu();
    for (unsigned type = GARDEN_BULLET_DART; type <= GARDEN_BULLET_DIAMOND; ++type) {
        for (unsigned colour = 1U; colour <= GARDEN_BULLET_COLOURS; ++colour) {
            unsigned selected = 0U;
            for (unsigned rotation = 1U; rotation < GARDEN_PRECISION_ROTATIONS; ++rotation) {
                if (!(rotation & 7U)) continue; /* Existing 8-direction ID. */
                unsigned id = garden_bullet_precision_id(type, colour, rotation * 16U);
                Patch patch;
                if (!select_patch(id, &patch)) continue;
                ++stats.sprites;
                ++stats.high_region_sources;
                if (stats.sprites == 1U) stats.first_source = stats.min_source = patch.src;
                stats.last_source = patch.src;
                if (patch.src < stats.min_source) stats.min_source = patch.src;
                if (patch.src > stats.max_source) stats.max_source = patch.src;
                rc = verify_case(&patch, 32U);
                if (rc != GPU_OK) goto restore;
                rc = verify_case(&patch, 16U);
                if (rc != GPU_OK) goto restore;
                if (++selected == 2U) break;
            }
            if (selected != 2U) { rc = GPU_EHW; goto restore; }
        }
    }
    rc = GPU_OK;
restore:
    stats.exit_status = GPU_STATUS;
    if (stats.exit_status & STATUS_BUSY) {
        stats.restore_skipped = 1U;
        if (rc == GPU_OK) rc = GPU_ESTATE;
    } else {
        /* Never replay a saved start bit. No previous command is resumed. */
        GPU_CTRL = 0U;
        GPU_ALPHA = saved;
    }
    return rc;
}

const GpuPrecisionVerifyStats *gpu_precision_verify_stats(void) { return &stats; }
