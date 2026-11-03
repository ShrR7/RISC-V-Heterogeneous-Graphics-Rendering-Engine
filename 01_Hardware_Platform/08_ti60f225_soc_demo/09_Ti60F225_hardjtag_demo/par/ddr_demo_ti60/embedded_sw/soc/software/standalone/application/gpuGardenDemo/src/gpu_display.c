#include "gpu_display.h"

#include "gpu_regs.h"
#include "gpu_driver.h"

#define DISPLAY_SWAP_POLL_MAX 20000000U

static uint32_t g_front_sel;
static uint32_t g_back_sel;
static int g_in_frame;
static display_stats_t g_stats;

uint32_t display_get_active(void)
{
    return GPU_FB_STATUS & FB_STATUS_ACTIVE_MASK;
}

uint32_t display_swap_pending(void)
{
    return (GPU_FB_STATUS & FB_STATUS_PENDING_MASK) != 0U ? 1U : 0U;
}

uint32_t display_addr_of(uint32_t sel)
{
    return (sel & 1U) ? FB1_ADDR : FB0_ADDR;
}

int display_init(void)
{
    g_front_sel = display_get_active();
    g_back_sel = g_front_sel ^ 1U;
    g_in_frame = 0;
    display_stats_reset();
    return GPU_OK;
}

int display_request_fb(uint32_t sel)
{
    uint32_t want = sel & 1U;
    uint32_t active = display_get_active();

    GPU_FB_CTRL = want;
    g_stats.req_writes++;
    if (want == active) {
        /* Writing the active buffer is a legal no-op: no pending is raised. */
        g_stats.noop_writes++;
    }
    return GPU_OK;
}

int display_wait_swap(uint32_t sel, uint32_t timeout)
{
    uint32_t want = sel & 1U;
    uint32_t polls = 0U;
    uint32_t limit = (timeout == 0U) ? DISPLAY_SWAP_POLL_MAX : timeout;

    while (polls < limit) {
        if (display_get_active() == want) {
            if (polls > g_stats.max_polls) g_stats.max_polls = polls;
            return GPU_OK;
        }
        polls++;
    }
    g_stats.timeouts++;
    return GPU_ETIMEOUT;
}

void render_begin(void)
{
    /* The back buffer is always the one the display is not reading. */
    g_front_sel = display_get_active();
    g_back_sel = g_front_sel ^ 1U;
    g_in_frame = 1;
}

int render_end(uint32_t timeout)
{
    int rc;

    if (!g_in_frame) return GPU_ESTATE;

    /* Every draw must have retired before the buffer becomes visible. */
    rc = gpu_wait_idle(3000000U);
    if (rc != GPU_OK) {
        g_in_frame = 0;
        return rc;
    }

    display_request_fb(g_back_sel);
    rc = display_wait_swap(g_back_sel, timeout);
    if (rc == GPU_OK) {
        g_front_sel = g_back_sel;
        g_back_sel = g_front_sel ^ 1U;
        g_stats.swaps++;
    }
    g_in_frame = 0;
    return rc;
}

int display_probe(void)
{
    uint32_t active = display_get_active();
    uint32_t want = active ^ 1U;
    uint32_t polls;

    GPU_FB_CTRL = want;
    if (display_swap_pending() != 0U) {
        /* Request differs from active: the handshake registers exist. */
        return GPU_OK;
    }

    /* No pending bit: either the registers are absent or the swap was already
     * applied.  Wait a couple of frames to tell the two cases apart. */
    for (polls = 0U; polls < 6000000U; polls++) {
        if (display_get_active() == want) return GPU_OK;
    }

    /* Restore the original request so the display side is left consistent. */
    GPU_FB_CTRL = active;
    return GPU_EHW;
}

uint32_t display_back_sel(void)   { return g_back_sel; }
uint32_t display_back_addr(void)  { return display_addr_of(g_back_sel); }
uint32_t display_front_sel(void)  { return g_front_sel; }
uint32_t display_front_addr(void) { return display_addr_of(g_front_sel); }

int display_is_front(uint32_t addr)
{
    uint32_t base = display_addr_of(display_get_active());
    return (addr >= base && addr < base + FB_SIZE_BYTES) ? 1 : 0;
}

int display_check_render_target(uint32_t addr)
{
    if (display_is_front(addr)) {
        g_stats.front_rejects++;
        return GPU_EINVAL;
    }
    return GPU_OK;
}

const display_stats_t *display_stats(void) { return &g_stats; }

void display_stats_reset(void)
{
    g_stats.swaps = 0U;
    g_stats.timeouts = 0U;
    g_stats.req_writes = 0U;
    g_stats.noop_writes = 0U;
    g_stats.max_polls = 0U;
    g_stats.front_rejects = 0U;
}
