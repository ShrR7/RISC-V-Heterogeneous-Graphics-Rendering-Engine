/* GPL-3.0-or-later. Opaque source copy with verified absent key. */
#include "pure_bullet_test.h"
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "garden_log.h"
#define BASIC_PIXELS ((const uint16_t *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET))
static void publish(void)
{asm volatile("fence iorw,iorw" ::: "memory");data_cache_invalidate_all();asm volatile("fence iorw,iorw" ::: "memory");}
int basic_mode_probe(void)
{
    unsigned key_pixels=0U,no_key_matches=0U;
    for(unsigned i=0;i<27U*256U;i++){key_pixels+=BASIC_PIXELS[i]==0xF81FU;no_key_matches+=BASIC_PIXELS[i]==4U;}
    if(!key_pixels || no_key_matches)return GPU_EINVAL;
    unsigned errors=0U;const uint32_t src=ASSET_BASE_ADDR+ASSET_SIZE_BYTES-1024U,dst=src+128U;
    for(unsigned i=0;i<32U;i++){
        uint16_t value=i%3U==0U?0xF81FU:i%3U==1U?0U:(uint16_t)(0x181FU+i*997U);
        *(volatile uint16_t *)(uintptr_t)(src+i*2U)=value;
        *(volatile uint16_t *)(uintptr_t)(dst+i*2U)=0x39E7U;
    }
    publish();int rc=gpu_bitblt(src,dst,8,4,16,16,4U);if(rc)return rc;publish();
    for(unsigned i=0;i<32U;i++)errors+=*(const volatile uint16_t *)(uintptr_t)(dst+i*2U)!=*(const volatile uint16_t *)(uintptr_t)(src+i*2U);
    unsigned alpha_enabled=(GPU_CTRL&GPU_CTRL_ALPHA_ENABLE)!=0U;errors+=alpha_enabled;
    garden_log_printf("[BASIC_PIXEL_PROBE] rc=%u errors=%u pixels=32 original_key_pixels=%u source_pixels=6912 key=0x4 key_matches=%u alpha_enabled=%u copy_all=1 bake_black_at_setup=1\r\n",errors?1U:0U,errors,key_pixels,no_key_matches,alpha_enabled);
    return errors?GPU_EHW:GPU_OK;
}
