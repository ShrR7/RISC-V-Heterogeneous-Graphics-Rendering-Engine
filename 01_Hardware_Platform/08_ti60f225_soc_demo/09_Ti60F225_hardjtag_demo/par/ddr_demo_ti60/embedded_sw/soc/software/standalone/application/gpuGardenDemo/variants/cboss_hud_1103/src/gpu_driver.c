#include "gpu_driver.h"

#include "gpu_regs.h"
#include "vexriscv.h"

#define GPU_POLL_MAX      3000000U
/* gpu_bitblt.v / gpu_copy.v line buffer: MAX_LINE_BEATS * 16 */
#define GPU_MAX_ROW_BYTES 4096U

static uint32_t g_last_status;
static uint32_t g_last_polls;
#ifdef GPU_DRIVER_GEOMETRY_HOST_TEST
/* Test-only work counters; no firmware state or timing instrumentation. */
static uint64_t geometry_fastpaths, geometry_row_checks;
#endif

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

    /* The complete half-open bounding ranges have already passed the 64-bit
     * address/stride checks above. Atlas -> framebuffer commands always have
     * disjoint ranges, so avoid walking every source/destination row. Keep
     * the exact row merge for overlapping bounds (including legal row gaps).
     */
    if (check_src && (src_last + row_bytes <= dst ||
                      dst_last + row_bytes <= src)) {
#ifdef GPU_DRIVER_GEOMETRY_HOST_TEST
        geometry_fastpaths++;
#endif
        return GPU_OK;
    }

    /* Merge the monotonic row intervals to detect true overlap. */
    while (src_row < height && dst_row < height) {
#ifdef GPU_DRIVER_GEOMETRY_HOST_TEST
        geometry_row_checks++;
#endif
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

#ifdef GPU_DRIVER_GEOMETRY_HOST_TEST
int gpu_driver_geometry_contract(uint32_t src, uint32_t dst, uint32_t width,
    uint32_t height, uint32_t src_stride, uint32_t dst_stride,
    int check_src, int check_overlap)
{
    return check_geom(src,dst,width,height,src_stride,dst_stride,check_src,check_overlap);
}
void gpu_driver_geometry_work_reset(void)
{ geometry_fastpaths = geometry_row_checks = 0U; }
void gpu_driver_geometry_work(uint64_t *fastpaths, uint64_t *rows)
{ *fastpaths = geometry_fastpaths; *rows = geometry_row_checks; }
#endif

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


/* Host seam is confined to the new batch path; the established single-command
 * driver above is unchanged. There are no extra firmware queues or OCM arrays. */
#ifdef GPU_DRIVER_BATCH_HOST_TEST
extern void gpu_batch_test_write(uint32_t offset, uint32_t value);
extern uint32_t gpu_batch_test_read(uint32_t offset);
extern void gpu_batch_test_fence(void);
extern void gpu_batch_test_invalidate(void);
#define batch_write(o,v) gpu_batch_test_write((o),(v))
#define batch_read(o) gpu_batch_test_read(o)
#define batch_fence() gpu_batch_test_fence()
#define batch_invalidate() gpu_batch_test_invalidate()
#else
static void batch_write(uint32_t offset, uint32_t value)
{ *(volatile uint32_t *)(GPU_BASE + offset) = value; }
static uint32_t batch_read(uint32_t offset)
{ return *(volatile uint32_t *)(GPU_BASE + offset); }
#define batch_fence() __asm__ volatile("fence iorw, iorw" ::: "memory")
#define batch_invalidate() data_cache_invalidate_all()
#endif

_Static_assert(sizeof(GpuFillEntry) == 8U, "RTL12 Fill entry ABI");
_Static_assert(sizeof(GpuBlitEntry) == 4U, "RTL12 Blit entry ABI");

static int batch_reject(int rc)
{ g_last_status = 0U; g_last_polls = 0U; return rc; }

static int batch_list_check(const void *list, uint32_t count,
                            uint32_t entry_size, uint32_t *addr, uint32_t *bytes)
{
    uintptr_t pointer = (uintptr_t)list;
    uint64_t padded = ((uint64_t)count * entry_size + 15U) & ~15ULL;
    if (count > 1024U || pointer > UINT32_MAX || (pointer & 15U) != 0U)
        return GPU_EINVAL;
    *addr = (uint32_t)pointer;
    *bytes = (uint32_t)padded;
    if (count == 0U) return GPU_OK;
    /* Only the DDR aperture already allocated/reviewed by this C firmware is
     * accepted. No assumption is made from the much larger CPU bus decode. */
    if (pointer < 0x00100000U || (uint64_t)pointer + padded >
        0x08000000ULL) return GPU_EINVAL;
    return GPU_OK;
}

static inline __attribute__((always_inline)) int batch_list_overlaps(uint32_t list_addr, uint32_t list_bytes,
                               uint32_t addr, uint32_t width, uint32_t height,
                               uint32_t stride)
{
    uint64_t end;
    if (width == 0U || height == 0U) return 0;
    end = (uint64_t)addr + (uint64_t)(height - 1U) * stride + (uint64_t)width * 2U;
    /* Deliberately reject the complete bounding range, including row gaps:
     * the command list is immutable input, never framebuffer scratch. */
    return (uint64_t)list_addr < end &&
        (uint64_t)addr < (uint64_t)list_addr + list_bytes;
}

static int batch_alpha_rows(uint32_t src, uint32_t dst, uint32_t width,
                            uint32_t height, uint32_t ss, uint32_t ds)
{
    uint32_t rows = height < 8U ? height : 8U;
    for (uint32_t row = 0U; row < rows; ++row) {
        uint32_t sl = (uint32_t)(((uint64_t)src + (uint64_t)row * ss) & 15U);
        uint32_t dl = (uint32_t)(((uint64_t)dst + (uint64_t)row * ds) & 15U);
        if ((uint64_t)width * 2U + sl > GPU_MAX_ROW_BYTES ||
            (uint64_t)width * 2U + dl > GPU_MAX_ROW_BYTES) return GPU_EINVAL;
    }
    return GPU_OK;
}

/* Unlike the historical single-command routine, an ERROR while BUSY is not a
 * completion. Keep waiting for final drain; never mutate live parameters. */
static int batch_wait(int completion)
{
    uint32_t status = 0U, seen_error = 0U, polls = 0U;
    while (polls < GPU_POLL_MAX) {
        status = batch_read(0x04U);
        seen_error |= status & STATUS_ERROR;
        if ((status & STATUS_BUSY) == 0U &&
            (!completion || (status & (STATUS_DONE | STATUS_ERROR)) != 0U)) {
            g_last_status = status; g_last_polls = polls;
            return seen_error ? GPU_EHW : GPU_OK;
        }
        ++polls;
    }
    g_last_status = status; g_last_polls = polls;
    return GPU_ETIMEOUT;
}

static void batch_publish(void)
{
    /* BSP 0x500f invalidates this SoC's write-through data cache. The fences
     * order CPU DDR stores before GPU list/source reads, and prevent compiler
     * reordering across the BSP macro (which has no memory clobber itself). */
    batch_fence(); batch_invalidate(); batch_fence();
}

static int batch_run(uint32_t ctrl)
{
    int rc;
    batch_write(0x00U, 0U);
    batch_write(0x00U, ctrl);
    rc = batch_wait(1);
    if (rc != GPU_ETIMEOUT) {
        /* Also make any partially completed writes visible after a drained
         * error. The caller still receives EHW and must stop this render. */
        batch_fence(); batch_invalidate(); batch_fence();
    }
    return rc;
}

int gpu_fill_batch(const GpuFillEntry *list, uint32_t count,
                   uint32_t dst_stride, uint16_t color)
{
    uint32_t addr = 0U, bytes = 0U;
    int rc = batch_list_check(list, count, sizeof(*list), &addr, &bytes);
    if (rc != GPU_OK) return batch_reject(rc);
    for (uint32_t i = 0U; i < count; ++i) {
        rc = check_geom(0U, list[i].dst, list[i].width, list[i].height,
                        0U, dst_stride, 0, 0);
        if (rc != GPU_OK) return batch_reject(rc);
        if (batch_list_overlaps(addr, bytes, list[i].dst, list[i].width,
                               list[i].height, dst_stride))
            return batch_reject(GPU_EOVERLAP);
    }
    if (count == 0U) return batch_reject(GPU_OK);
    rc = batch_wait(0);
    if (rc != GPU_OK) return rc;
    batch_publish();
    batch_write(0x1CU, dst_stride); batch_write(0x20U, color);
    batch_write(0x34U, addr); batch_write(0x38U, count);
    return batch_run(GPU_CTRL_FILL_BATCH);
}

int gpu_bitblt_batch(const GpuBlitEntry *list, uint32_t count, uint32_t src,
                     uint32_t width, uint32_t height, uint32_t src_stride,
                     uint32_t dst_stride, uint16_t key, uint32_t alpha)
{
    uint32_t addr = 0U, bytes = 0U;
    int rc = batch_list_check(list, count, sizeof(*list), &addr, &bytes);
    if (alpha > 32U) rc = GPU_EINVAL;
    if (rc != GPU_OK) return batch_reject(rc);
    if (batch_list_overlaps(addr, bytes, src, width, height, src_stride))
        return batch_reject(GPU_EOVERLAP);
    for (uint32_t i = 0U; i < count; ++i) {
        rc = check_geom(src, list[i].dst, width, height, src_stride,
                        dst_stride, 1, 1);
        if (rc == GPU_OK && alpha < 32U)
            rc = batch_alpha_rows(src, list[i].dst, width, height,
                                   src_stride, dst_stride);
        if (rc != GPU_OK) return batch_reject(rc);
        if (batch_list_overlaps(addr, bytes, list[i].dst, width,
                                height, dst_stride))
            return batch_reject(GPU_EOVERLAP);
    }
    if (count == 0U) return batch_reject(GPU_OK);
    rc = batch_wait(0);
    if (rc != GPU_OK) return rc;
    batch_publish();
    batch_write(0x08U, src); batch_write(0x10U, width);
    batch_write(0x14U, height); batch_write(0x18U, src_stride);
    batch_write(0x1CU, dst_stride); batch_write(0x20U, key);
    batch_write(0x30U, alpha); batch_write(0x34U, addr);
    batch_write(0x38U, count);
    return batch_run(alpha == 32U ? GPU_CTRL_BITBLT_BATCH :
                     GPU_CTRL_ALPHA_BITBLT_BATCH);
}
