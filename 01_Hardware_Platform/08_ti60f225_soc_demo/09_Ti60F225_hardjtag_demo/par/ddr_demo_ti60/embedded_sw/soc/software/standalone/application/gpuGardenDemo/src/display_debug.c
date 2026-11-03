/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bsp.h"
#include "gpu_regs.h"
#include "display_debug.h"

#define DREG(offset) (*(volatile uint32_t *)(GPU_BASE+(offset)))
#define DBG_MAGIC 0x44424731U
typedef struct {
    uint32_t before,after,ctrl,px,lines,stall,row,stamp,ticks;
} DebugSnapshot;
static unsigned available,have_previous;
static uint32_t identity,previous;
static uint32_t samples,px_sum,line_sum,stall_sum,px_sat,line_sat,stall_sat;
static uint32_t status_or,gap_max;
static DebugSnapshot last,event;

void display_debug_sample(void)
{
    uint32_t gap;
    if (!available) return;
    /* Ordered volatile reads; counters clear independently, NOT an atomic
     * snapshot. An RTL increment coincident with a read can be discarded. */
    last.ticks=clint_getTimeLow(BSP_CLINT);
    last.before=DREG(0x50U);
    last.ctrl=DREG(0x5CU);
    last.px=DREG(0x54U)&0xffffU;
    last.lines=DREG(0x58U)&0xffffU;
    last.stall=DREG(0x60U)&0xffffU;
    last.row=DREG(0x64U)&0xffffU;
    last.stamp=DREG(0x68U)&0xffffU;
    last.after=DREG(0x50U);
    gap=have_previous ? last.ticks-previous : 0U;
    previous=last.ticks; have_previous=1U;
    if (gap>gap_max) gap_max=gap;
    /* Sums cover one short report interval, not lifetime totals. Saturated
     * samples are lower bounds only; their incidence is logged separately. */
    samples++; px_sum+=last.px; line_sum+=last.lines; stall_sum+=last.stall;
    px_sat+=last.px==65535U; line_sat+=last.lines==65535U;
    stall_sat+=last.stall==65535U;
    status_or|=last.before|last.after;
    if (last.px || last.lines || last.stall) event=last;
}

unsigned display_debug_init(void)
{
    identity=DREG(0x6CU); available=identity==DBG_MAGIC;
    samples=px_sum=line_sum=stall_sum=px_sat=line_sat=stall_sat=0U;
    status_or=gap_max=have_previous=0U;
    event=(DebugSnapshot){0}; last=(DebugSnapshot){0};
    display_debug_sample();
    return available;
}

void display_debug_report(const char *phase,uint32_t frame)
{
    if (!available) {
        bsp_printf("[DISPLAY_DBG] phase=%s present=0 id=0x%x frame=%d\r\n",phase,identity,frame);
        return;
    }
    bsp_printf("[DISPLAY_DBG] phase=%s present=1 id=0x%x frame=%d samples=%d px_sum=%d line_sum=%d stall_sum=%d px_sat=%d line_sat=%d stall_sat=%d status_or=0x%x max_gap_ticks=%d ctrl=0x%x raw_px=%d raw_line=%d raw_stall=%d raw_row=%d status_before=0x%x status_after=0x%x stamp=%d ticks=0x%x event_px=%d event_line=%d event_stall=%d event_row=%d event_status=0x%x event_ticks=0x%x clear_all_written=0 exact_HDMI_D=UNMEASURED\r\n",
        phase,identity,frame,samples,px_sum,line_sum,stall_sum,px_sat,line_sat,
        stall_sat,status_or,gap_max,last.ctrl,last.px,last.lines,last.stall,
        last.row,last.before,last.after,last.stamp,last.ticks,event.px,
        event.lines,event.stall,event.row,event.before,event.ticks);
    samples=px_sum=line_sum=stall_sum=px_sat=line_sat=stall_sat=0U;
    status_or=gap_max=0U; event=(DebugSnapshot){0};
}
