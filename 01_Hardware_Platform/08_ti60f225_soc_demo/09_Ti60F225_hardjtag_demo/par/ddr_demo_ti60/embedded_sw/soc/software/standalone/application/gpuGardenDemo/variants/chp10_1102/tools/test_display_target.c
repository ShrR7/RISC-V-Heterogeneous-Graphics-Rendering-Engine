/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production display lifecycle with only MMIO/time/idle modeled on the host.
 * No framebuffer pixels, renderer, core, or firmware test branches are used. */
#include "gpu_display.h"
#include "gpu_driver.h"
#include "gpu_regs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef EXPECT_TARGET_CACHE
#define EXPECT_TARGET_CACHE 1
#endif
static uint32_t status_value,status_reads,ctrl_writes,clock_value;
static uint32_t checks,clean_calls,clean_reads;
static uint32_t sequence[4],sequence_count,sequence_pos;
static int write_mode,idle_result,check_write_invalidation;
static void require(int ok,const char *why,int line)
{ checks++;if(!ok){fprintf(stderr,"FAIL line=%d %s\n",line,why);exit(1);} }
#define CHECK(x) require((x),#x,__LINE__)

uint32_t host_fb_status(void)
{
    status_reads++;
    if(sequence_pos<sequence_count) return sequence[sequence_pos++];
    return status_value;
}
void host_fb_write(uint32_t value)
{
    ctrl_writes++;
    if(write_mode==1) status_value=value&1U; /* swap already applied */
    else if(write_mode==0)
        status_value=(status_value&1U)|(((value&1U)!=(status_value&1U))?2U:0U);
    if(check_write_invalidation) {
        uint32_t before=status_reads,front=status_value&1U;
        CHECK(display_is_front(display_addr_of(front))==1);
        CHECK(status_reads==before+1U); /* invalid before the write callback */
    }
}
uint32_t clint_getTimeLow(uint32_t ignored)
{ (void)ignored;return ++clock_value; }
int gpu_wait_idle(uint32_t timeout)
{ CHECK(timeout==3000000U);return idle_result; }

static void reset(uint32_t front)
{
    status_value=front;status_reads=ctrl_writes=clock_value=0U;
    sequence_count=sequence_pos=0U;write_mode=0;
    idle_result=GPU_OK;check_write_invalidation=0;
    CHECK(display_init()==GPU_OK);
    CHECK(status_reads==1U);
    CHECK(display_front_sel()==front && display_back_sel()==(front^1U));
}
static void live_boundaries(uint32_t front)
{
    uint32_t base=display_addr_of(front),before=status_reads;
    CHECK(display_is_front(base-1U)==0);
    CHECK(display_is_front(base)==1);
    CHECK(display_is_front(base+FB_SIZE_BYTES-1U)==1);
    CHECK(display_is_front(base+FB_SIZE_BYTES)==0);
    CHECK(display_is_front(display_addr_of(front^1U))==0);
    CHECK(status_reads==before+5U);
}
static void clean_frame(uint32_t front)
{
    reset(front);live_boundaries(front);
    uint32_t before=status_reads;
    render_begin();CHECK(status_reads==before+1U);
    CHECK(display_front_addr()==display_addr_of(front));
    CHECK(display_back_addr()==display_addr_of(front^1U));
    before=status_reads;
    for(unsigned i=0;i<1000U;i++) {
        CHECK(display_check_render_target(display_addr_of(front^1U)+i*2U)==GPU_OK);
        CHECK(display_check_render_target(display_addr_of(front)+i*2U)==GPU_EINVAL);
    }
    clean_calls+=2000U;clean_reads+=status_reads-before;
    CHECK(status_reads-before==(EXPECT_TARGET_CACHE?0U:2000U));
    CHECK(display_stats()->front_rejects==1000U);
    /* Public state reads remain live, even when target checks use the cache. */
    before=status_reads;
    CHECK(display_get_active()==front && display_swap_pending()==0U);
    CHECK(status_reads==before+2U);
    write_mode=1;check_write_invalidation=1;
    CHECK(render_end(3U)==GPU_OK);
    CHECK(display_stats()->swaps==1U && ctrl_writes==1U);
    CHECK(display_front_sel()==(front^1U));
    CHECK(display_frame_timing()->gpu_idle_ticks==1U);
    CHECK(display_frame_timing()->swap_wait_ticks==1U);
    live_boundaries(front^1U);
    before=status_reads;CHECK(render_end(3U)==GPU_ESTATE);
    CHECK(status_reads==before);
}
static void pending_and_requests(uint32_t front)
{
    reset(front);status_value=front|2U;
    uint32_t before=status_reads;render_begin();CHECK(status_reads==before+1U);
    status_value=front^1U; /* an earlier pending request completes */
    live_boundaries(front^1U);
    /* Reentering begin refreshes both active and pending in one snapshot. */
    sequence[0]=front;sequence[1]=(front^1U)|2U;
    sequence_count=2U;sequence_pos=0U;
    before=status_reads;render_begin();CHECK(status_reads==before+1U);
    CHECK(sequence_pos==1U && display_front_sel()==front);
    sequence_count=sequence_pos=0U;status_value=front;
    write_mode=1;check_write_invalidation=1;
    CHECK(display_request_fb(front^1U)==GPU_OK);
    live_boundaries(front^1U);
    render_begin();
    CHECK(display_request_fb(front^1U)==GPU_OK); /* no-op also invalidates */
    CHECK(display_stats()->noop_writes==1U);
    live_boundaries(front^1U);
    render_begin();status_value=front; /* init must discard cached target */
    CHECK(display_init()==GPU_OK);live_boundaries(front);
}
static void end_failures(uint32_t front)
{
    reset(front);render_begin();idle_result=GPU_ETIMEOUT;
    CHECK(render_end(3U)==GPU_ETIMEOUT && ctrl_writes==0U);
    status_value=front^1U;live_boundaries(front^1U);
    reset(front);render_begin(); /* pending swap does not finish before timeout */
    CHECK(render_end(3U)==GPU_ETIMEOUT);
    CHECK(ctrl_writes==1U && display_stats()->timeouts==1U);
    CHECK(display_front_sel()==front);
    status_value=front^1U;live_boundaries(front^1U);
    render_begin();idle_result=GPU_EHW;
    CHECK(render_end(3U)==GPU_EHW);live_boundaries(front^1U);
}
static void probe_paths(uint32_t front)
{
    reset(front);render_begin();check_write_invalidation=1;
    CHECK(display_probe()==GPU_OK); /* pending-raised path */
    CHECK(ctrl_writes==1U);status_value=front^1U;live_boundaries(front^1U);
    reset(front);render_begin();write_mode=1;check_write_invalidation=1;
    CHECK(display_probe()==GPU_OK); /* applied before pending read */
    CHECK(ctrl_writes==1U);live_boundaries(front^1U);
    reset(front);render_begin();write_mode=2;check_write_invalidation=1;
    CHECK(display_probe()==GPU_EHW); /* missing registers, restore request */
    CHECK(ctrl_writes==2U);live_boundaries(front);
}
int main(void)
{
    for(uint32_t front=0;front<2U;front++) {
        clean_frame(front);pending_and_requests(front);
        end_failures(front);probe_paths(front);
    }
    printf("DISPLAY_TARGET checks=%u clean_guard_calls=%u clean_status_reads=%u cache=%d fronts=2\n",
           checks,clean_calls,clean_reads,EXPECT_TARGET_CACHE);
    return 0;
}
