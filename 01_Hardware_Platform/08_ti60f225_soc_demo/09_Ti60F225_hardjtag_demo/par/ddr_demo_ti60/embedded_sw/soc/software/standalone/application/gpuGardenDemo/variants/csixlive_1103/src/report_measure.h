/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

#define REPORT_MEASURE_BASE 0x00D00000U
#define REPORT_MEASURE_SAMPLE_ADDR (REPORT_MEASURE_BASE+256U)
#define REPORT_MEASURE_CAPACITY 40000U
#define REPORT_MEASURE_MAGIC 0x31545052U /* RPT1, little endian */
#define REPORT_MEASURE_REVISION 1U
enum { REPORT_KIND_GAME=0U, REPORT_KIND_STRESS=1U };
enum { REPORT_STATUS_IDLE=0U, REPORT_STATUS_ACTIVE=1U, REPORT_STATUS_DONE=2U,
       REPORT_STATUS_ABORTED=3U, REPORT_STATUS_FAILED=4U };
typedef struct {
    uint32_t period_ticks,service_ticks,draw_ticks,clear_ticks;
    uint32_t gpu_idle_ticks,swap_ticks,visible,checksum;
} ReportMeasureSample;
typedef struct {
    uint32_t magic,revision,header_bytes,sample_bytes;
    uint32_t capacity,samples,status,kind;
    uint32_t backend,scene,objects,alpha;
    uint32_t timer_hz,duration_seconds,start_lo,start_hi;
    uint32_t last_lo,last_hi,end_lo,end_hi;
    uint32_t windows,visible_min,visible_max,overflow;
    uint32_t missing_px,missing_lines,reject,skip;
    uint32_t guard,log_dropped,log_truncated,errors;
    uint32_t reserved[28];
    uint32_t header_guard[4];
} ReportMeasureHeader;
_Static_assert(sizeof(ReportMeasureSample)==32U,"report sample ABI");
_Static_assert(sizeof(ReportMeasureHeader)==256U,"report header ABI");
#define REPORT_MEASURE_GUARD_ADDR (REPORT_MEASURE_SAMPLE_ADDR+REPORT_MEASURE_CAPACITY*32U)
_Static_assert(REPORT_MEASURE_GUARD_ADDR+16U<=0x00E80000U,"report DDR envelope");

/* begin's timestamp is one successfully completed presentation, excluded from
 * samples. Each frame stores the interval from that completion/the last one.
 * done() finalizes after duration, emits any partial final window and END.
 * Samples stay in DDR for GDB dump until a later begin() explicitly replaces
 * them. No per-frame UART output. Native ticks are authoritative. */
int report_measure_begin(unsigned kind,unsigned backend,unsigned scene,
    unsigned count,unsigned alpha,unsigned seconds,uint64_t start_completed);
int report_measure_frame(uint64_t completed,uint32_t service_ticks,
    uint32_t draw_ticks,uint32_t clear_ticks,uint32_t visible,uint32_t checksum);
unsigned report_measure_done(void);
void report_measure_abort(void);
unsigned report_measure_active(void);
void report_measure_deferred(unsigned enabled);
void report_measure_flush(void);
#define REPORT_DISPLAY_ADDR 0x00E40000U
#define REPORT_DISPLAY_GUARD_ADDR (REPORT_DISPLAY_ADDR+REPORT_MEASURE_CAPACITY*12U)
