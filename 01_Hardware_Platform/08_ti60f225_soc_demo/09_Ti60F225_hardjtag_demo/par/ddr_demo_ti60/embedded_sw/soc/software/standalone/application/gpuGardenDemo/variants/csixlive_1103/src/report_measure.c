/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "report_measure.h"
#include "bsp.h"
#include "gpu_display.h"
#include "garden_log.h"
#include "stage9_link.h"
#include "display_debug.h"
#include "g9_memory.h"
#include <string.h>
#ifndef REPORT_MEASURE_HOST_TEST
#include "vexriscv.h"
#define HEAD ((volatile ReportMeasureHeader *)(uintptr_t)REPORT_MEASURE_BASE)
#define SAMPLES ((volatile ReportMeasureSample *)(uintptr_t)REPORT_MEASURE_SAMPLE_ADDR)
#define GUARD ((volatile uint32_t *)(uintptr_t)REPORT_MEASURE_GUARD_ADDR)
static void publish(void){asm volatile("fence iorw,iorw" ::: "memory");data_cache_invalidate_all();}
#else
extern ReportMeasureHeader report_host_header;
extern ReportMeasureSample report_host_samples[REPORT_MEASURE_CAPACITY];
extern uint32_t report_host_guard[4];
#define HEAD (&report_host_header)
#define SAMPLES report_host_samples
#define GUARD report_host_guard
static void publish(void){}
#endif
#define LO(t) (uint32_t)(t)
#define HI(t) (uint32_t)((t)>>32)
static struct {
    uint64_t start,last,window;
    uint32_t win_samples,visible_min,visible_max,drop,trunc;
    Stage9LinkStats first,previous;
    DisplayDebugTotals display;
} measure;
static unsigned active,deferred,start_pending,window_pending;
#ifndef REPORT_MEASURE_HOST_TEST
#define DISPLAY_SAMPLES ((volatile uint32_t *)(uintptr_t)REPORT_DISPLAY_ADDR)
#define DISPLAY_GUARD ((volatile uint32_t *)(uintptr_t)REPORT_DISPLAY_GUARD_ADDR)
#endif
void report_measure_deferred(unsigned enabled){deferred=enabled;}
static uint32_t guard_value(unsigned i){return 0xC7011100U+i;}
static unsigned guards(void)
{
    unsigned bad=0U;
    for(unsigned i=0;i<4U;i++)bad+=(HEAD->header_guard[i]!=guard_value(i))+(GUARD[i]!=guard_value(i+4U));
    return bad;
}
static void finish(unsigned status)
{
    HEAD->status=status;HEAD->end_lo=LO(measure.last);HEAD->end_hi=HI(measure.last);
    active=0U;publish();
    garden_log_printf("[RPT_END] kind=%u backend=%u scene=%u n=%u start=0x%08x%08x end=0x%08x%08x span=0x%08x%08x hz=%u status=%u guard=%u overflow=%u drop=%u trunc=%u base=0x%x bytes=%u\r\n",
        HEAD->kind,HEAD->backend,HEAD->scene,HEAD->samples,HI(measure.start),LO(measure.start),
        HI(measure.last),LO(measure.last),HI(measure.last-measure.start),LO(measure.last-measure.start),
        HEAD->timer_hz,status,HEAD->guard,HEAD->overflow,HEAD->log_dropped,HEAD->log_truncated,
        REPORT_MEASURE_BASE,256U+HEAD->samples*32U);
    const GardenLogStats *logs=garden_log_stats();
    HEAD->log_dropped=logs->dropped_lines-measure.drop;
    HEAD->log_truncated=logs->truncated_lines-measure.trunc;
    publish();garden_log_pump();
}
static void window_report(void)
{
    if(!measure.win_samples)return;
    Stage9LinkStats now;stage9_link_snapshot(&now);
    const GardenLogStats *logs=garden_log_stats();
    HEAD->missing_px=now.missing_px-measure.first.missing_px;
    HEAD->missing_lines=now.missing_lines-measure.first.missing_lines;
    HEAD->reject=now.rejected-measure.first.rejected;
    HEAD->skip=now.skipped-measure.first.skipped;
    HEAD->errors=now.errors-measure.first.errors;
    HEAD->log_dropped=logs->dropped_lines-measure.drop;
    HEAD->log_truncated=logs->truncated_lines-measure.trunc;
    HEAD->guard+=g9_memory_guard_check()+guards();
    uint64_t span=measure.last-measure.window;
    garden_log_printf("[RPT_WIN] seq=%u n=%u total=%u span=0x%08x%08x end=0x%08x%08x hz=%u fps10=%u visible_min=%u visible_max=%u miss_px=%u miss_lines=%u reject=%u skip=%u guard=%u errors=%u drop=%u trunc=%u\r\n",
        HEAD->windows,measure.win_samples,HEAD->samples,HI(span),LO(span),HI(measure.last),LO(measure.last),
        HEAD->timer_hz,(uint32_t)((uint64_t)measure.win_samples*HEAD->timer_hz*10U/span),
        measure.visible_min,measure.visible_max,now.missing_px-measure.previous.missing_px,
        now.missing_lines-measure.previous.missing_lines,now.rejected-measure.previous.rejected,
        now.skipped-measure.previous.skipped,HEAD->guard,HEAD->errors,HEAD->log_dropped,HEAD->log_truncated);
    const DisplayDebugTotals *disp=display_debug_totals();
    uint64_t px=disp->px-measure.display.px,line=disp->lines-measure.display.lines,full=disp->full-measure.display.full;
    unsigned sat=0U;
    for(unsigned i=0;i<5U;i++)sat|=(disp->saturated_samples[i]!=measure.display.saturated_samples[i])<<i;
    garden_log_printf("[RPT_DISP] seq=%u px=0x%08x%08x lines=0x%08x%08x full=0x%08x%08x sat=%u status=0x%x\r\n",
        HEAD->windows,HI(px),LO(px),HI(line),LO(line),HI(full),LO(full),sat,disp->status);
    measure.previous=now;measure.display=*disp;measure.window=measure.last;
    measure.win_samples=0U;measure.visible_min=UINT32_MAX;measure.visible_max=0U;HEAD->windows++;
    garden_log_pump();
}
static void start_report(void)
{
    garden_log_printf("[RPT_START] kind=%u backend=%u scene=%u count=%u alpha=%u seconds=%u t=0x%08x%08x hz=%u sample=0x%x stride=32 capacity=40000 warmup_excluded=1 period=completed_to_completed\r\n",
        HEAD->kind,HEAD->backend,HEAD->scene,HEAD->objects,HEAD->alpha,HEAD->duration_seconds,HI(measure.start),LO(measure.start),HEAD->timer_hz,REPORT_MEASURE_SAMPLE_ADDR);
    garden_log_pump();
}
void report_measure_flush(void)
{
    if(start_pending){start_report();start_pending=0U;}
    if(window_pending){window_report();window_pending=0U;}
}
int report_measure_begin(unsigned kind,unsigned backend,unsigned scene,
    unsigned count,unsigned alpha,unsigned seconds,uint64_t start_completed)
{
    if(active || !seconds || seconds>600U || backend>2U || scene>3U || alpha>32U)return -1;
    memset((void *)HEAD,0,sizeof(*HEAD));memset(&measure,0,sizeof(measure));
    HEAD->magic=REPORT_MEASURE_MAGIC;HEAD->revision=REPORT_MEASURE_REVISION;
    HEAD->header_bytes=256U;HEAD->sample_bytes=32U;HEAD->capacity=REPORT_MEASURE_CAPACITY;
    HEAD->kind=kind;HEAD->backend=backend;HEAD->scene=scene;HEAD->objects=count;HEAD->alpha=alpha;
    HEAD->timer_hz=BSP_CLINT_HZ;HEAD->duration_seconds=seconds;
    HEAD->start_lo=LO(start_completed);HEAD->start_hi=HI(start_completed);
    HEAD->last_lo=LO(start_completed);HEAD->last_hi=HI(start_completed);
    HEAD->visible_min=UINT32_MAX;
    for(unsigned i=0;i<4U;i++){HEAD->header_guard[i]=guard_value(i);GUARD[i]=guard_value(i+4U);}
    measure.start=measure.last=measure.window=start_completed;measure.visible_min=UINT32_MAX;
    stage9_link_snapshot(&measure.first);measure.previous=measure.first;
    display_debug_sample();measure.display=*display_debug_totals();
    const GardenLogStats *logs=garden_log_stats();measure.drop=logs->dropped_lines;measure.trunc=logs->truncated_lines;
#ifndef REPORT_MEASURE_HOST_TEST
    HEAD->reserved[0]=REPORT_DISPLAY_ADDR;HEAD->reserved[1]=12U;
    for(unsigned i=0;i<4U;i++)DISPLAY_GUARD[i]=0xC7022200U+i;
#endif
    HEAD->status=REPORT_STATUS_ACTIVE;active=1U;publish();
    start_pending=1U;window_pending=0U;
    if(!deferred)report_measure_flush();
    return 0;
}
int report_measure_frame(uint64_t completed,uint32_t service_ticks,
    uint32_t draw_ticks,uint32_t clear_ticks,uint32_t visible,uint32_t checksum)
{
    if(!active)return -1;
    uint64_t period=completed-measure.last;
    unsigned bad=guards();
    if(completed<=measure.last || period>UINT32_MAX || HEAD->samples>=REPORT_MEASURE_CAPACITY || bad){
        HEAD->overflow+=period>UINT32_MAX || HEAD->samples>=REPORT_MEASURE_CAPACITY;
        HEAD->guard+=bad;finish(REPORT_STATUS_FAILED);return -2;
    }
    const display_frame_timing_t *display=display_frame_timing();
    volatile ReportMeasureSample *out=SAMPLES+HEAD->samples;
    *out=(ReportMeasureSample){(uint32_t)period,service_ticks,draw_ticks,clear_ticks,
        display->gpu_idle_ticks,display->swap_wait_ticks,visible,checksum};
    HEAD->samples++;measure.win_samples++;measure.last=completed;
    HEAD->last_lo=LO(completed);HEAD->last_hi=HI(completed);
    if(visible<measure.visible_min)measure.visible_min=visible;
    if(visible>measure.visible_max)measure.visible_max=visible;
    if(visible<HEAD->visible_min)HEAD->visible_min=visible;
    if(visible>HEAD->visible_max)HEAD->visible_max=visible;
    const DisplayDebugTotals before=*display_debug_totals();
    display_debug_sample();
#ifndef REPORT_MEASURE_HOST_TEST
    const DisplayDebugTotals *after=display_debug_totals();
    unsigned di=(HEAD->samples-1U)*3U;
    DISPLAY_SAMPLES[di]=(uint32_t)(after->px-before.px);
    DISPLAY_SAMPLES[di+1U]=(uint32_t)(after->lines-before.lines);
    DISPLAY_SAMPLES[di+2U]=(uint32_t)(after->full-before.full);
#else
    (void)before;
#endif
    if(completed-measure.window>=(uint64_t)BSP_CLINT_HZ*2U){if(deferred)window_pending=1U;else window_report();}
    return 0;
}
unsigned report_measure_done(void)
{
    if(!active)return HEAD->status==REPORT_STATUS_DONE;
    if(measure.last-measure.start<(uint64_t)HEAD->duration_seconds*BSP_CLINT_HZ)return 0U;
    report_measure_flush();window_report();finish(HEAD->guard?REPORT_STATUS_FAILED:REPORT_STATUS_DONE);return 1U;
}
void report_measure_abort(void)
{
    if(active){report_measure_flush();window_report();finish(REPORT_STATUS_ABORTED);}
}
unsigned report_measure_active(void){return active;}
