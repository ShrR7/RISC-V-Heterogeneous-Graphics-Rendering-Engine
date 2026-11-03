/* SPDX-License-Identifier: GPL-3.0-or-later
 * Isolated rtl7 Garden copy: retain main/renderer/assets; replace only the
 * existing display_debug hooks. Do not link src/display_debug.c as well.
 */
#include <stdint.h>
#include "display_debug.h"
static unsigned counter7_init(void);
static void counter7_sample(void);
static void counter7_report(const char *phase, uint32_t frame);
#define display_debug_init counter7_init
#define display_debug_sample counter7_sample
#define display_debug_report counter7_report
#include "main.c"
#undef display_debug_init
#undef display_debug_sample
#undef display_debug_report

#define C7REG(off) (*(volatile uint32_t *)(uintptr_t)(GPU_BASE+(off)))
#define C7MAGIC 0x44424731U
/* garden_log supports only 32-bit conversions: emit fixed high/low words. */
#define C7U64(v) (uint32_t)((v)>>32), (uint32_t)(v)
enum { C7PX, C7PART, C7AR, C7R, C7FULL, C7COUNT };
typedef struct {
    uint64_t t0, t1;
    uint32_t value[C7COUNT], raw_stall, status, status_after, row, q, pred, boot_pred;
} Counter7Sample;
typedef struct {
    Counter7Sample sample;
    uint32_t frame, tick, mode, score, valid;
} Counter7Event;

static Counter7Sample c7last;
static Counter7Event c7event, c7partial;
static uint32_t c7id, c7available, c7running, c7frames, c7samples, c7rid, c7refused;
static uint32_t c7previous_mode, c7previous_attack, c7have_previous, c7game_started;
static uint32_t c7sat[C7COUNT], c7total_sat[C7COUNT], c7max[C7COUNT];
static uint32_t c7overflow, c7total_overflow, c7status_or, c7q_or;
static uint32_t c7boot0, c7boot1, c7initial_q;
static uint64_t c7sum[C7COUNT], c7total[C7COUNT];
static uint64_t c7previous_t0, c7report_ticks, c7gap_bound, c7span_max, c7first_t0, c7game_t0;

/* Masks bits0..4 describe 64-bit count sum overflow; bits5..9 describe
 * 32-bit saturation-incidence counter overflow. Values clamp, never wrap. */
static void counter7_add64(uint64_t *sum, uint32_t value, uint32_t *overflow, unsigned bit)
{
    if (UINT64_MAX-*sum<value) { *sum=UINT64_MAX; *overflow|=1U<<bit; }
    else *sum+=value;
}
static void counter7_add32(uint32_t *sum, uint32_t value, uint32_t *overflow, unsigned bit)
{
    if (UINT32_MAX-*sum<value) { *sum=UINT32_MAX; *overflow|=1U<<bit; }
    else *sum+=value;
}

static void counter7_sample(void)
{
    uint64_t span, bound;
    if (!c7available) return;
    c7last.t0=clint_getTime(BSP_CLINT);
    c7last.status=C7REG(0x50U);
    c7last.value[C7PX]=C7REG(0x54U)&0xffffU;
    c7last.value[C7PART]=C7REG(0x58U)&0xffffU;
    c7last.raw_stall=C7REG(0x60U); /* ONCE: both halves are read-cleared. */
    c7last.value[C7AR]=c7last.raw_stall&0xffffU;
    c7last.value[C7R]=c7last.raw_stall>>16;
    c7last.row=C7REG(0x64U)&0xffffU;
    c7last.value[C7FULL]=C7REG(0x70U)&0xffffU;
    c7last.q=C7REG(0x74U);
    c7last.pred=C7REG(0x80U);
    c7last.boot_pred=C7REG(0x84U);
    c7last.status_after=C7REG(0x50U);
    c7last.t1=clint_getTime(BSP_CLINT);
    span=c7last.t1-c7last.t0;
    if (span>c7span_max) c7span_max=span;
    /* For EVERY read-clear field, previous read >= previous t0 and current
     * read <= current t1. This is an upper bound, NOT an exact read instant.
     * Includes sampling duration, UART/report work and any stopped CPU time. */
    bound=c7have_previous ? c7last.t1-c7previous_t0 : 0U;
    if (bound>c7gap_bound) c7gap_bound=bound;
    if (!c7have_previous) c7first_t0=c7last.t0;
    c7previous_t0=c7last.t0; c7have_previous=1U;
    c7samples++;
    if (c7running) c7frames++;
    for (unsigned i=0;i<C7COUNT;i++) {
        counter7_add64(&c7sum[i],c7last.value[i],&c7overflow,i);
        counter7_add64(&c7total[i],c7last.value[i],&c7total_overflow,i);
        counter7_add32(&c7sat[i],c7last.value[i]==65535U,&c7overflow,C7COUNT+i);
        counter7_add32(&c7total_sat[i],c7last.value[i]==65535U,&c7total_overflow,C7COUNT+i);
        if (c7last.value[i]>c7max[i]) c7max[i]=c7last.value[i];
    }
    c7status_or|=c7last.status|c7last.status_after; c7q_or|=c7last.q;
    if (c7last.value[C7PX] || c7last.value[C7PART] || c7last.value[C7AR] ||
        c7last.value[C7R] || c7last.value[C7FULL]) {
        c7event=(Counter7Event){c7last,c7frames,game.tick,(uint32_t)game.mode,game.score,1U};
        /* Later ordinary waits MUST NOT replace the latest PART row/time. */
        if (c7last.value[C7PART]) c7partial=c7event;
    }
    if (c7running) {
        uint32_t attack=game.boss.active ? game.boss.attack : 0xffffffffU;
        if ((uint32_t)game.mode!=c7previous_mode || attack!=c7previous_attack)
            counter7_report("STATE",c7frames);
        else if (c7last.t1-c7report_ticks>=4ULL*BSP_CLINT_HZ)
            counter7_report("SECOND",c7frames);
    }
}

static unsigned counter7_init(void)
{
    c7id=C7REG(0x6CU); c7available=c7id==C7MAGIC;
    if (c7available) {
        c7initial_q=C7REG(0x74U);
        c7boot0=C7REG(0x78U); c7boot1=C7REG(0x7CU);
        counter7_sample();
    }
    return c7available;
}

static void counter7_report(const char *phase, uint32_t frame)
{
    uint32_t ctrl, fb, q, boot0, boot1, rid, sat_mask=0U;
    const Counter7Event *ev;
    int accepted;
    /* Start device timing even if the log queue refuses this report. */
    if (!c7game_started && !strcmp(phase,"GAME_START")) {
        c7game_started=c7running=1U; c7frames=0U; c7game_t0=clint_getTime(BSP_CLINT);
    }
    if (!c7available) {
        garden_log_printf("[DISPLAY7] phase=%s present=0 id=0x%08x frame=%u\r\n",phase,c7id,frame);
        return;
    }
    if (!c7samples) return;
    ctrl=C7REG(0x5CU); fb=GPU_FB_STATUS; q=C7REG(0x74U);
    boot0=C7REG(0x78U); boot1=C7REG(0x7CU);
    rid=++c7rid; ev=c7partial.valid ? &c7partial : &c7event;
    for (unsigned i=0;i<C7COUNT;i++) if (c7sat[i]) sat_mask|=1U<<i;
    /* Two records form one report, joined by rid. A refused record keeps the
     * entire window; a later attempt has a NEW rid and may repeat its history.
     * Consumers sum windows only for complete DISPLAY7+DISPLAY7_AUX pairs. */
    accepted=garden_log_printf("[DISPLAY7] phase=%s present=1 rid=%u frame=%u tick=%u mode=%u attack=%u score=%u lives=%u backend=%s alpha=%u panel=%u fb=0x%x ctrl=0x%x t=0x%08x%08x..0x%08x%08x n=%u gb=0x%08x%08x span=0x%08x%08x sum=0x%08x%08x/0x%08x%08x/0x%08x%08x/0x%08x%08x/0x%08x%08x sat=%u/%u/%u/%u/%u max=%u/%u/%u/%u/%u last=%u/%u/%u/%u/%u rawstall=0x%08x row=%u s=0x%x/0x%x sor=0x%x q=0x%x qor=0x%x pred=0x%x/0x%x sm=0x%x ov=0x%x\r\n",
        phase,rid,frame,game.tick,(uint32_t)game.mode,game.boss.attack,game.score,game.lives,
        garden_render_is_cpu()?"CPU":"GPU",(unsigned)garden_render_alpha_enabled(),garden_render_alpha_level(),
        fb,ctrl,C7U64(c7last.t0),C7U64(c7last.t1),c7samples,C7U64(c7gap_bound),C7U64(c7span_max),
        C7U64(c7sum[C7PX]),C7U64(c7sum[C7PART]),C7U64(c7sum[C7AR]),C7U64(c7sum[C7R]),C7U64(c7sum[C7FULL]),
        c7sat[C7PX],c7sat[C7PART],c7sat[C7AR],c7sat[C7R],c7sat[C7FULL],
        c7max[C7PX],c7max[C7PART],c7max[C7AR],c7max[C7R],c7max[C7FULL],
        c7last.value[C7PX],c7last.value[C7PART],c7last.value[C7AR],c7last.value[C7R],c7last.value[C7FULL],
        c7last.raw_stall,c7last.row,c7last.status,c7last.status_after,c7status_or,q,c7q_or,
        c7last.pred,c7last.boot_pred,sat_mask,c7overflow);
    if (accepted<0) { if (c7refused!=UINT32_MAX) c7refused++; return; }
    accepted=garden_log_printf("[DISPLAY7_AUX] rid=%u total=0x%08x%08x/0x%08x%08x/0x%08x%08x/0x%08x%08x/0x%08x%08x total_sat=%u/%u/%u/%u/%u tov=0x%x event=%u/%u/%u/%u/%u eraw=0x%08x erow=%u et=0x%08x%08x..0x%08x%08x es=0x%x/0x%x eq=0x%x ev=%u ep=%u estate=%u/%u/%u/%u boot_first=0x%x/0x%x/0x%x boot_now=0x%x/0x%x first=0x%08x%08x game_start=0x%08x%08x game_elapsed=0x%08x%08x refused=%u snapshot_not_runtime=1 pred_not_D=1 clear_all_written=0\r\n",
        rid,C7U64(c7total[C7PX]),C7U64(c7total[C7PART]),C7U64(c7total[C7AR]),C7U64(c7total[C7R]),C7U64(c7total[C7FULL]),
        c7total_sat[C7PX],c7total_sat[C7PART],c7total_sat[C7AR],c7total_sat[C7R],c7total_sat[C7FULL],c7total_overflow,
        ev->sample.value[C7PX],ev->sample.value[C7PART],ev->sample.value[C7AR],ev->sample.value[C7R],ev->sample.value[C7FULL],
        ev->sample.raw_stall,ev->sample.row,C7U64(ev->sample.t0),C7U64(ev->sample.t1),
        ev->sample.status,ev->sample.status_after,ev->sample.q,ev->valid,(unsigned)(c7partial.valid!=0U),
        ev->frame,ev->tick,ev->mode,ev->score,c7initial_q,c7boot0,c7boot1,boot0,boot1,
        C7U64(c7first_t0),C7U64(c7game_t0),C7U64(c7game_started && c7last.t1>=c7game_t0 ? c7last.t1-c7game_t0 : 0U),c7refused);
    if (accepted<0) { if (c7refused!=UINT32_MAX) c7refused++; return; }
    c7samples=c7status_or=c7q_or=c7overflow=0U;
    c7gap_bound=c7span_max=0U;
    for (unsigned i=0;i<C7COUNT;i++) { c7sum[i]=0U; c7sat[i]=c7max[i]=0U; }
    c7event=(Counter7Event){0}; c7partial=(Counter7Event){0};
    c7report_ticks=c7last.t1;
    c7previous_mode=(uint32_t)game.mode;
    c7previous_attack=game.boss.active ? game.boss.attack : 0xffffffffU;
}

