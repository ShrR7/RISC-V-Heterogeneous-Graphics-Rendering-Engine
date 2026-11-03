/* GPL-3.0-or-later. Six exclusive full-screen scenes, CPU and GPU matching effects; live metrics, COM11 RAM-only. */
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "gpu_alpha_verify.h"
#include "gpu_precision_verify.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_log.h"
#include "g9_memory.h"
#include "stage9_link.h"
#include "display_debug.h"
#include "pad_hw.h"
extern int modes_run(unsigned backend,unsigned mode,unsigned count,unsigned seconds);
extern int modes_probe(void);
void trap_entry(void);
void trap(void)
{
    bsp_printf("[TRAP] cause=0x%x pc=0x%x value=0x%x\r\n",(unsigned)csr_read(mcause),(unsigned)csr_read(mepc),(unsigned)csr_read(mtval));
    while(1){}
}
static unsigned tx_free(void){return uart_writeAvailability(BSP_UART_TERMINAL);}
static void tx_put(char c){uart_write(BSP_UART_TERMINAL,c);}
static void fatal(const char *why)
{garden_log_set_async(0);garden_log_printf("[FATAL] %s\r\n",why);while(1){}}
static uint32_t crc32(const uint8_t *p,unsigned n)
{
    uint32_t c=0xffffffffU;
    for(unsigned i=0;i<n;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}
    return c^0xffffffffU;
}
int main(void)
{
    bsp_init();csr_write(mtvec,trap_entry);garden_log_bind(tx_free,tx_put);
    if(GPU_ID!=GPU_MAGIC_ID || stage9_link_init())fatal("hardware ID");
    g9_memory_guard_init();g9_memory_reset();display_debug_init();
    if(crc32((const uint8_t *)(uintptr_t)GARDEN_ATLAS_ADDRESS,GARDEN_ATLAS_BYTES)!=GARDEN_ATLAS_CRC32)fatal("atlas CRC");
    display_init();if(display_probe())fatal("swap probe");
    for(unsigned i=0;i<61U;i++){unsigned s=display_get_active()^1U;display_request_fb(s);if(display_wait_swap(s,20000000U))fatal("swap calibration");}
    garden_render_init();
    int rc=gpu_alpha_verify();const GpuAlphaVerifyStats *a=gpu_alpha_verify_stats();
    garden_log_printf("[ALPHA_PROBE] rc=%d cases=%u pixel_errors=%u source_errors=%u guard_errors=%u\r\n",rc,a->cases,a->pixel_errors,a->source_errors,a->guard_errors);
    if(rc)fatal("alpha probe");
    garden_render_alpha_support(1);
    rc=gpu_precision_verify();const GpuPrecisionVerifyStats *p=gpu_precision_verify_stats();
    garden_log_printf("[PRECISION_PROBE] rc=%d cases=%u pixel_errors=%u source_errors=%u guard_errors=%u\r\n",rc,p->cases,p->pixel_errors,p->source_errors,p->guard_errors);
    if(rc || modes_probe())fatal("mode pixel probe");
    garden_log_set_async(1);display_stats_reset();
    garden_log_printf("[READY] SIXLIVE_1103 cases=6 duration_ms=10000 count=2093 independent=1 cpu_effects=matched metrics=live flash_write=0\r\n");
    for(unsigned cycle=0U;;cycle++){
        for(unsigned mode=0U;mode<3U;mode++){
            for(unsigned pass=0U;pass<2U;pass++){
                unsigned backend=pass==0U?1U:0U;
                unsigned case_id=mode*2U+pass+1U;
                garden_log_printf("[SIXL_CASE] cycle=%u case=%u backend=%u mode=%u count=2093\r\n",cycle,case_id,backend,mode);
                rc=modes_run(backend,mode,2093U,10U);
                garden_log_printf("[SIXL_CASE_DONE] cycle=%u case=%u backend=%u mode=%u rc=%d\r\n",cycle,case_id,backend,mode,rc);
                if(rc)fatal("six-case renderer");
                uint64_t drain_until=clint_getTime(BSP_CLINT)+BSP_CLINT_HZ/10U;
                while(garden_log_stats()->queued && clint_getTime(BSP_CLINT)<drain_until)garden_log_pump();
            }
        }
    }
    return 0;
}
