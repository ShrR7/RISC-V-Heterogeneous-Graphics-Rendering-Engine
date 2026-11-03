#include "gpu_driver.h"

#include "gpu_regs.h"
#include "vexriscv.h"

#define GPU_POLL_MAX      3000000U
/* gpu_bitblt.v / gpu_copy.v line buffer: MAX_LINE_BEATS * 16 */
#define GPU_MAX_ROW_BYTES 4096U

static uint32_t g_last_status;
static uint32_t g_last_polls;

static uint32_t gpu_run(uint32_t ctrl)
{
    uint32_t status = 0U;
    uint32_t polls = 0U;

    /* CTRL.start is edge-detected; force a low-to-high transition. */
    GPU_CTRL = 0U;
    GPU_CTRL = ctrl;

    while (polls < GPU_POLL_MAX) {
        status = GPU_STATUS;
        if ((status & STATUS_ERROR) != 0U ||
            ((status & STATUS_DONE) != 0U && (status & STATUS_BUSY) == 0U)) {
            break;
        }
        polls++;
    }

    g_last_status = status;
    g_last_polls = polls;
    if ((status & STATUS_ERROR) != 0U) return GPU_EHW;
    if ((status & STATUS_DONE) == 0U || (status & STATUS_BUSY) != 0U) {
        return GPU_ETIMEOUT;
    }
    /* GPU writes DDR3 behind the CPU cache; invalidate before CPU read-back. */
    data_cache_invalidate_all();
    return GPU_OK;
}

/*
 * Shared geometry check.
 *
 * `src` is only used when the engine reads a source; Fill passes src == 0 and
 * check_src == 0.  The 4 KB line limit mirrors gpu_bitblt.v/gpu_copy.v exactly:
 * the engine rounds ceil16(src[3:0] + 2*width), so an unaligned source start
 * reduces the usable width.
 */
static int check_geom(uint32_t src, uint32_t dst, uint32_t width,
                      uint32_t height, uint32_t src_stride,
                      uint32_t dst_stride, int check_src, int check_overlap)
{
    uint64_t row_bytes;
    uint64_t src_last;
    uint64_t dst_last;
    uint32_t src_row = 0U;
    uint32_t dst_row = 0U;

    if (width > 0xFFFFU || height > 0xFFFFU || (dst & 1U) != 0U) {
        return GPU_EINVAL;
    }
    if (check_src && (src & 1U) != 0U) return GPU_EINVAL;

    row_bytes = (uint64_t)width * 2U;
    if (row_bytes > GPU_MAX_ROW_BYTES) return GPU_EINVAL;
    if (check_src &&
        row_bytes + (uint64_t)(src & 0xFU) > GPU_MAX_ROW_BYTES) {
        return GPU_EINVAL;
    }
    if ((dst_stride & 1U) != 0U || dst_stride < row_bytes) return GPU_EINVAL;
    if (check_src &&
        ((src_stride & 1U) != 0U || src_stride < row_bytes)) {
        return GPU_EINVAL;
    }

    /* Empty operations are legal and complete without AXI traffic. */
    if (width == 0U || height == 0U) return GPU_OK;

    dst_last = (uint64_t)dst + (uint64_t)(height - 1U) * dst_stride;
    if (dst_last + row_bytes > 0x100000000ULL) return GPU_EINVAL;
    if (check_src) {
        src_last = (uint64_t)src + (uint64_t)(height - 1U) * src_stride;
        if (src_last + row_bytes > 0x100000000ULL) return GPU_EINVAL;
    }

    if (!check_overlap) return GPU_OK;

    /* Merge the monotonic row intervals to detect true overlap. */
    while (src_row < height && dst_row < height) {
        uint64_t sb = (uint64_t)src + (uint64_t)src_row * src_stride;
        uint64_t db = (uint64_t)dst + (uint64_t)dst_row * dst_stride;
        uint64_t se = sb + row_bytes;
        uint64_t de = db + row_bytes;
        if (sb < de && db < se) return GPU_EOVERLAP;
        if (se <= db) src_row++;
        else dst_row++;
    }
    return GPU_OK;
}

int gpu_fill(uint32_t dst, uint32_t width, uint32_t height,
             uint32_t dst_stride, uint16_t color)
{
    int rc = check_geom(0U, dst, width, height, 0U, dst_stride, 0, 0);
    if (rc != GPU_OK) {
        g_last_status = 0U;
        g_last_polls = 0U;
        return rc;
    }
    GPU_DST_ADDR   = dst;
    GPU_WIDTH      = width;
    GPU_HEIGHT     = height;
    GPU_DST_STRIDE = dst_stride;
    GPU_COLOR      = (uint32_t)color;
    return gpu_run(GPU_CTRL_FILL);
}

int gpu_copy(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
             uint32_t src_stride, uint32_t dst_stride)
{
    int rc = check_geom(src, dst, width, height, src_stride, dst_stride, 1, 1);
    if (rc != GPU_OK) {
        g_last_status = 0U;
        g_last_polls = 0U;
        return rc;
    }
    GPU_SRC_ADDR   = src;
    GPU_DST_ADDR   = dst;
    GPU_WIDTH      = width;
    GPU_HEIGHT     = height;
    GPU_SRC_STRIDE = src_stride;
    GPU_DST_STRIDE = dst_stride;
    return gpu_run(GPU_CTRL_COPY);
}

int gpu_bitblt(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
               uint32_t src_stride, uint32_t dst_stride, uint16_t key)
{
    int rc = check_geom(src, dst, width, height, src_stride, dst_stride, 1, 1);
    if (rc != GPU_OK) {
        g_last_status = 0U;
        g_last_polls = 0U;
        return rc;
    }
    GPU_SRC_ADDR   = src;
    GPU_DST_ADDR   = dst;
    GPU_WIDTH      = width;
    GPU_HEIGHT     = height;
    GPU_SRC_STRIDE = src_stride;
    GPU_DST_STRIDE = dst_stride;
    GPU_COLOR      = (uint32_t)key;
    return gpu_run(GPU_CTRL_BITBLT);
}

int gpu_alpha_bitblt(uint32_t src, uint32_t dst, uint32_t width, uint32_t height,
                     uint32_t src_stride, uint32_t dst_stride,
                     uint16_t key, uint32_t alpha)
{
    int rc = check_geom(src, dst, width, height, src_stride, dst_stride, 1, 1);
    /* Alpha reads the destination into a second 4 KiB line buffer. */
    if (alpha > 32U || (uint64_t)width * 2U + (dst & 0xFU) > GPU_MAX_ROW_BYTES) {
        rc = GPU_EINVAL;
    }
    /* With even byte strides, alignment lanes repeat within eight rows.
     * Validate both read buffers on each distinct lane, not only row zero. */
    if (rc == GPU_OK) {
        uint32_t rows = height < 8U ? height : 8U;
        for (uint32_t row = 0U; row < rows; ++row) {
            uint32_t src_lane = (uint32_t)(((uint64_t)src + (uint64_t)row * src_stride) & 0xFU);
            uint32_t dst_lane = (uint32_t)(((uint64_t)dst + (uint64_t)row * dst_stride) & 0xFU);
            if ((uint64_t)width * 2U + src_lane > GPU_MAX_ROW_BYTES ||
                (uint64_t)width * 2U + dst_lane > GPU_MAX_ROW_BYTES) {
                rc = GPU_EINVAL;
                break;
            }
        }
    }
    if (rc != GPU_OK) {
        g_last_status = 0U;
        g_last_polls = 0U;
        return rc;
    }
    /* Alpha/CTRL are live engine outputs. A timed-out earlier command may
     * still be running: never mutate its parameters before it is idle. */
    rc = gpu_wait_idle(GPU_POLL_MAX);
    if (rc != GPU_OK) {
        g_last_status = GPU_STATUS;
        g_last_polls = GPU_POLL_MAX;
        return rc;
    }
    GPU_SRC_ADDR   = src;
    GPU_DST_ADDR   = dst;
    GPU_WIDTH      = width;
    GPU_HEIGHT     = height;
    GPU_SRC_STRIDE = src_stride;
    GPU_DST_STRIDE = dst_stride;
    GPU_COLOR      = (uint32_t)key;
    GPU_ALPHA      = alpha;
    return gpu_run(GPU_CTRL_ALPHA_BITBLT);
}

uint32_t gpu_last_status(void) { return g_last_status; }
uint32_t gpu_last_polls(void)  { return g_last_polls; }

int gpu_busy(void)
{
    return (GPU_STATUS & STATUS_BUSY) != 0U;
}

int gpu_wait_idle(uint32_t timeout)
{
    uint32_t polls = 0U;
    while (polls < timeout) {
        if (!gpu_busy()) return GPU_OK;
        polls++;
    }
    return GPU_ETIMEOUT;
}
