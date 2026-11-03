/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gpu_regs.h"
#include "display_debug.h"
#include "garden_log.h"
#include <string.h>
#define DREG(o) (*(volatile uint32_t *)(uintptr_t)(GPU_BASE+(o)))
static unsigned available;
static DisplayDebugTotals totals;
void display_debug_sample(void)
{
    if(!available)return;
    uint32_t px=DREG(0x54U)&65535U,lines=DREG(0x58U)&65535U;
    uint32_t stall=DREG(0x60U),full=DREG(0x70U)&65535U;
    uint32_t ar=stall&65535U,r=stall>>16;
    totals.px+=px;totals.lines+=lines;totals.full+=full;
    totals.stall_ar+=ar;totals.stall_r+=r;totals.samples++;
    totals.status|=DREG(0x50U);
    uint32_t values[5]={px,lines,full,ar,r};
    for(unsigned i=0;i<5U;i++)totals.saturated_samples[i]+=values[i]==65535U;
    totals.saturation|=(px==65535U)|((lines==65535U)<<1)|((full==65535U)<<2)|
        ((ar==65535U)<<3)|((r==65535U)<<4);
}
unsigned display_debug_init(void)
{
    memset(&totals,0,sizeof(totals));
    available=DREG(0x6cU)==0x44424731U;display_debug_sample();return available;
}
const DisplayDebugTotals *display_debug_totals(void){return &totals;}
void display_debug_report(const char *phase,uint32_t frame)
{
    garden_log_printf("[DISP] phase=%s frame=%u present=%u px=0x%08x%08x lines=0x%08x%08x full=0x%08x%08x sat=%u status=0x%x\r\n",
        phase,frame,available,(uint32_t)(totals.px>>32),(uint32_t)totals.px,
        (uint32_t)(totals.lines>>32),(uint32_t)totals.lines,
        (uint32_t)(totals.full>>32),(uint32_t)totals.full,totals.saturation,totals.status);
}
