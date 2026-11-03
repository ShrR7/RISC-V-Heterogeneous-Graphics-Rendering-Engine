/* GPL-3.0-or-later. Low-level Stage9 handoff; no gameplay or atlas mapping. */
#include "stage9_link.h"
#include "gpu_regs.h"
#include "gpu_display.h"
#include <string.h>
#ifdef STAGE9_LINK_HOST_TEST
extern uint32_t stage9_link_test_read(unsigned off);
extern void stage9_link_test_write(unsigned off,uint32_t value);
extern uint64_t stage9_link_test_now(void);
extern void stage9_link_test_fence(void);
#define HZ 100000000U
#define rd(o) stage9_link_test_read(o)
#define wr(o,v) stage9_link_test_write((o),(v))
#define now() stage9_link_test_now()
#define barrier() stage9_link_test_fence()
#define pump() ((void)0)
#else
#include "bsp.h"
#include "vexriscv.h"
#include "garden_log.h"
#define pump() garden_log_pump()
#define HZ BSP_CLINT_HZ
static uint32_t rd(unsigned o){return *(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x100U+o);}
static void wr(unsigned o,uint32_t v){*(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x100U+o)=v;}
static uint64_t now(void){return clint_getTime(BSP_CLINT);}
static void barrier(void){asm volatile("fence iorw,iorw" ::: "memory");}
#endif
#define ID 0x42554C39U
#define WAIT_TICKS ((uint64_t)HZ/10U)
#define CONFIG_TICKS ((uint64_t)HZ*3U/1000U)
#define ACTIVE_MASK 0x383U
#define IDLE_CONFIG_MASK 0x283U
#define ERROR_MASK 0x430U
static Stage9LinkStats st;
static unsigned failed;
static unsigned blend_alpha=32U;
static uint16_t benchmark_key=0xF81FU;
void stage9_link_key(uint16_t key){benchmark_key=key;}
void stage9_link_alpha(unsigned alpha){blend_alpha=alpha;}
static uint64_t pumped;
static void service(void)
{uint64_t t=now();if(t-pumped>=(uint64_t)HZ/10000U){pump();pumped=t;}}
static void raw_snapshot(void)
{
    if(!st.available)return;
    st.status=rd(0x0cU);st.displayed_frames=rd(0x34U);st.engine_frames=rd(0x38U);
    st.missing_pixels=rd(0x2cU);st.missing_lines=rd(0x30U);st.skipped_frames=rd(0x3cU);
    st.rejected_writes=rd(0x48U);st.max_line_cycles=rd(0x44U);st.last_frame_cycles=rd(0x4cU);
}
void stage9_link_snapshot(Stage9LinkStats *out)
{raw_snapshot();if(out)*out=st;}
static int stop_raw(void)
{
    if(!st.available)return STAGE9_LINK_EID;
    uint64_t start=now();wr(0x08U,8U);wr(0x04U,0U);barrier();
    while(rd(0x0cU)&ACTIVE_MASK){service();if(now()-start>WAIT_TICKS){st.fallback_ticks+=now()-start;return STAGE9_LINK_EDRAIN;}}
    st.fallback_ticks+=now()-start;st.running=0U;return 0;
}
static int error(int rc)
{
    st.last_error=rc;st.errors++;failed=1U;
    if(st.available)(void)stop_raw();
    raw_snapshot();return rc;
}
int stage9_link_stop(void)
{int rc=stop_raw();if(rc)return error(rc);return 0;}
int stage9_link_init(void)
{
    memset(&st,0,sizeof(st));failed=0U;blend_alpha=32U;pumped=now();
    /* ID read is the only access before proving that the new page exists. */
    if(rd(0U)!=ID)return error(STAGE9_LINK_EID);
    st.available=1U;
    uint32_t revision=rd(0x50U);
    if((revision!=0x00010007U)||rd(0x54U)!=2048U||rd(0x58U)!=32U)return error(STAGE9_LINK_EID);
    int rc=stop_raw();if(rc)return error(rc);
    raw_snapshot();st.initial_displayed_frames=st.displayed_frames;st.initial_engine_frames=st.engine_frames;
    st.initial_missing_pixels=st.missing_pixels;st.initial_missing_lines=st.missing_lines;
    st.initial_rejected_writes=st.rejected_writes;st.initial_skipped_frames=st.skipped_frames;
    return 0;
}
static int existing_fault(void)
{
    if(rd(0x0cU)&ERROR_MASK)return STAGE9_LINK_EFRAME;
    if(rd(0x48U)!=st.initial_rejected_writes)return STAGE9_LINK_EREJECT;
    if(rd(0x2cU)!=st.initial_missing_pixels||rd(0x30U)!=st.initial_missing_lines)return STAGE9_LINK_EFRAME;
    return 0;
}
static int valid_span(uint32_t addr,uint32_t bytes)
{return !(addr&15U)&&addr>=0x00800000U&&(uint64_t)addr+bytes<=0x01000000ULL;}
static int overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m)
{return (uint64_t)a+n>b&&(uint64_t)b+m>a;}
int stage9_link_present(uint32_t atlas,uint32_t desc,unsigned count,uint32_t bg,unsigned back_sel)
{
    if(!st.available||failed)return STAGE9_LINK_ESTATE;
    uint32_t db=count?count*16U:16U;
    if(count>2048U||back_sel>1U||bg!=display_addr_of(back_sel)||
       !valid_span(atlas,16384U)||!valid_span(desc,db)||overlap(atlas,16384U,desc,db))return error(STAGE9_LINK_EINVAL);
    if(display_get_active()==back_sel)return error(STAGE9_LINK_EFB);
    int rc=existing_fault();if(rc)return error(rc);
    barrier();
    uint64_t waited=now();
    if(st.running){
        uint32_t shown=rd(0x34U);
        while(rd(0x34U)==shown){
            service();rc=existing_fault();if(rc)return error(rc);
            if(now()-waited>WAIT_TICKS){st.wait_ticks+=now()-waited;return error(STAGE9_LINK_EDISPLAY);}
        }
    }
    st.wait_ticks+=now()-waited;
    rc=existing_fault();if(rc)return error(rc);
    unsigned was_running=st.running;
    uint64_t configured=now();
    /* New displayed-frame event is near row480, leaving~5.3ms before719.
     * Only3ms is budgeted, including cancelling and the one cached atlas load. */
    wr(0x08U,8U);barrier();
    while(rd(0x0cU)&IDLE_CONFIG_MASK){service();if(now()-configured>CONFIG_TICKS)return error(STAGE9_LINK_EDEADLINE);}
    wr(0x10U,atlas);wr(0x08U,1U);barrier();
    for(;;){
        service();uint32_t s=rd(0x0cU);
        if(s&0x10U)return error(STAGE9_LINK_EATLAS);
        if(!(s&1U)&&(s&0x44U)==0x44U)break;
        if(now()-configured>CONFIG_TICKS)return error(STAGE9_LINK_EDEADLINE);
    }
    wr(0x14U,desc);wr(0x18U,count);wr(0x1cU,bg);wr(0x20U,benchmark_key);wr(0x24U,blend_alpha==32U?32U:(0x100U|blend_alpha));barrier();
    if(rd(0x48U)!=st.initial_rejected_writes)return error(STAGE9_LINK_EREJECT);
    if(now()-configured>CONFIG_TICKS)return error(STAGE9_LINK_EDEADLINE);
    if(display_get_active()==back_sel)return error(STAGE9_LINK_EFB);
    if(display_request_fb(back_sel)!=0)return error(STAGE9_LINK_EFB);
    uint64_t elapsed=now()-configured;
    if(!st.running){
        /* On first entry do not enable at720 before the requested legacy swap
         * has actually occurred at719. render_end may repeat this wait safely. */
        uint64_t aligned=now();
        if(display_wait_swap(back_sel,20000000U)!=0){st.wait_ticks+=now()-aligned;return error(STAGE9_LINK_EFB);}
        st.wait_ticks+=now()-aligned;
    }
    uint64_t armed_at=now();
    wr(0x04U,3U);wr(0x08U,2U);barrier();
    elapsed=was_running?now()-configured:elapsed+(now()-armed_at);
    st.config_ticks+=elapsed;if(elapsed>st.max_config_ticks)st.max_config_ticks=elapsed;
    if(was_running&&elapsed>CONFIG_TICKS)return error(STAGE9_LINK_EDEADLINE);
    if(rd(0x48U)!=st.initial_rejected_writes)return error(STAGE9_LINK_EREJECT);
    st.running=1U;st.submissions++;st.last_error=0;raw_snapshot();return 0;
}
