/* Generated host-only contract; production atlas, metadata and probe code. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_precision_verify.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "garden_assets.h"
#include "gpu_alpha_ref.h"

#define PROBE_BASE (ASSET_BASE_ADDR+ASSET_SIZE_BYTES-2048U)
#define PANEL_BASE (ASSET_BASE_ADDR+ASSET_SIZE_BYTES-8192U)
static unsigned checks, command_count, mode, publishes, seen_families[10];
static uint32_t seen_sources[20], n_sources, dispatched_pixels;
static uint32_t framebuffer_hash, panel_hash, atlas_hash;
static const uint32_t controls[] = {0x28U,0x2CU,0x50U,0x54U,0x58U,0x5CU,0x60U,0x64U,0x68U,0x6CU,0x70U,0x74U,0x78U,0x7CU,0x80U,0x84U};
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(2); } } while(0)
static uint32_t hash_region(uint32_t base,unsigned bytes)
{
    const uint8_t *p=(const uint8_t *)(uintptr_t)base;
    uint32_t h=2166136261U;
    for(unsigned i=0;i<bytes;++i) h=(h^p[i])*16777619U;
    return h;
}
static void load_atlas(const char *path)
{
    FILE *f=fopen(path,"rb"); CHECK(f!=NULL);
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES);
    CHECK(fgetc(f)==EOF); fclose(f);
}
static void reset_registers(void)
{
    memset((void *)(uintptr_t)GPU_BASE,0,4096U);
    GPU_ID=GPU_MAGIC_ID; GPU_STATUS=STATUS_DONE; GPU_ALPHA=23U;
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);++i)
        *(volatile uint32_t *)(uintptr_t)(GPU_BASE+controls[i])=0xCA000000U+controls[i];
    memset((void *)(uintptr_t)PROBE_BASE,0xC7,2048U);
    mode=0; command_count=0; publishes=0; n_sources=0; dispatched_pixels=0;
    memset(seen_families,0,sizeof(seen_families));
}
static void save_immutable(void)
{
    framebuffer_hash=hash_region(FB0_ADDR,PANEL_BASE-FB0_ADDR);
    /* The first hash includes atlas but excludes panel/probe; these separate
       hashes give stronger explanatory evidence of each untouched region. */
    panel_hash=hash_region(PANEL_BASE,6144U);
    atlas_hash=hash_region(ASSET_BASE_ADDR,GARDEN_ATLAS_BYTES);
}
static void verify_immutable(int allow_atlas_change)
{
    if(!allow_atlas_change){
        CHECK(framebuffer_hash==hash_region(FB0_ADDR,PANEL_BASE-FB0_ADDR));
        CHECK(atlas_hash==hash_region(ASSET_BASE_ADDR,GARDEN_ATLAS_BYTES));
    }
    CHECK(panel_hash==hash_region(PANEL_BASE,6144U));
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);++i)
        CHECK(*(volatile uint32_t *)(uintptr_t)(GPU_BASE+controls[i])==0xCA000000U+controls[i]);
}
void gpu_precision_verify_host_publish(void) { ++publishes; }
void gpu_precision_verify_host_dispatch(void)
{
    ++command_count;
    dispatched_pixels+=GPU_WIDTH*GPU_HEIGHT;
    CHECK(GPU_CTRL==GPU_CTRL_BITBLT || GPU_CTRL==GPU_CTRL_ALPHA_BITBLT);
    CHECK(GPU_ALPHA==(GPU_CTRL==GPU_CTRL_BITBLT?32U:16U));
    CHECK(GPU_WIDTH>0U && GPU_WIDTH<=8U && GPU_HEIGHT>0U && GPU_HEIGHT<=8U);
    CHECK(GPU_SRC_ADDR>=0x600000U);
    CHECK(GPU_SRC_ADDR+((GPU_HEIGHT-1U)*GPU_SRC_STRIDE)+2U*GPU_WIDTH<=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES);
    CHECK(GPU_DST_ADDR>=PROBE_BASE+32U);
    CHECK(GPU_DST_ADDR+(GPU_HEIGHT-1U)*GPU_DST_STRIDE+2U*GPU_WIDTH<=ASSET_BASE_ADDR+ASSET_SIZE_BYTES);
    CHECK(GPU_COLOR==GARDEN_KEY_COLOR);
    unsigned id=0U;
    for(unsigned candidate=GARDEN_ASSET_PRECISION_BASE;candidate<GARDEN_ASSET_COUNT;++candidate){
        const garden_sprite_t *s=garden_asset(candidate);
        uint32_t begin=GARDEN_ATLAS_ADDRESS+s->offset;
        if(GPU_SRC_ADDR>=begin && GPU_SRC_ADDR<begin+garden_sprite_stride(s)*s->height){id=candidate;break;}
    }
    CHECK(id>=GARDEN_ASSET_PRECISION_BASE);
    ++seen_families[(id-GARDEN_ASSET_PRECISION_BASE)/56U];
    unsigned found=0U;
    for(unsigned i=0;i<n_sources;++i) found|=seen_sources[i]==GPU_SRC_ADDR;
    if(!found){CHECK(n_sources<20U);seen_sources[n_sources++]=GPU_SRC_ADDR;}
    if(mode==5U){GPU_STATUS=STATUS_BUSY;return;}
    if(mode==6U){GPU_STATUS=STATUS_ERROR;return;}
    for(unsigned y=0;y<GPU_HEIGHT;++y){
        const uint16_t *src=(const uint16_t *)(uintptr_t)(GPU_SRC_ADDR+y*GPU_SRC_STRIDE);
        uint16_t *dst=(uint16_t *)(uintptr_t)(GPU_DST_ADDR+y*GPU_DST_STRIDE);
        for(unsigned x=0;x<GPU_WIDTH;++x){
            if(src[x]!=GARDEN_KEY_COLOR)
                dst[x]=GPU_CTRL==GPU_CTRL_BITBLT?src[x]:gpu_alpha_rgb565(src[x],dst[x],GPU_ALPHA);
        }
    }
    if(mode==2U) *(uint16_t *)(uintptr_t)GPU_DST_ADDR^=1U;
    if(mode==3U) *(uint16_t *)(uintptr_t)(PROBE_BASE+2046U)^=1U;
    if(mode==4U) *(uint16_t *)(uintptr_t)GPU_SRC_ADDR^=1U;
    GPU_STATUS=STATUS_DONE;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    CHECK(VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)FB0_ADDR);
    CHECK(VirtualAlloc((void *)(uintptr_t)GPU_BASE,4096U,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)GPU_BASE);
    memset((void *)(uintptr_t)FB0_ADDR,0x5A,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR);
    load_atlas(argv[1]); reset_registers(); save_immutable();
    CHECK(gpu_precision_verify()==GPU_OK);
    GpuPrecisionVerifyStats success=*gpu_precision_verify_stats();
    CHECK(success.register_present==1U && success.cases==40U && success.commands==40U);
    CHECK(success.sprites==20U && success.high_region_sources==20U && n_sources==20U);
    CHECK(success.pixels==dispatched_pixels && success.pixels>0U && success.pixels<=2560U && success.key_pixels>0U && success.key_pixels<success.pixels);
    CHECK(success.opaque_cases==20U && success.alpha_cases==20U);
    CHECK(success.min_source>=0x600000U && success.max_source<ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES);
    CHECK(success.pixel_errors==0U && success.source_errors==0U && success.guard_errors==0U);
    CHECK(success.saved_alpha==23U && success.restore_skipped==0U && GPU_ALPHA==23U && GPU_CTRL==0U);
    CHECK(publishes>=81U);
    for(unsigned family=0;family<10U;++family) CHECK(seen_families[family]==4U);
    verify_immutable(0);
    printf("SUCCESS cases=%u sprites=%u pixels=%u key_pixels=%u min_source=0x%08X max_source=0x%08X immutable_FB_panel_atlas=1\n",success.cases,success.sprites,success.pixels,success.key_pixels,success.min_source,success.max_source);
    /* Faults must stop at their first command and restore Alpha only when idle. */
    for(unsigned fault=2U;fault<=6U;++fault){
        load_atlas(argv[1]); reset_registers(); save_immutable(); mode=fault;
        int rc=gpu_precision_verify(); const GpuPrecisionVerifyStats *s=gpu_precision_verify_stats();
        CHECK(rc==(fault==5U?GPU_ETIMEOUT:GPU_EHW));
        CHECK(command_count==1U && s->commands==1U);
        if(fault==2U) CHECK(s->pixel_errors>0U && s->guard_errors==0U);
        if(fault==3U) CHECK(s->guard_errors==1U && s->first_addr==PROBE_BASE+2046U);
        if(fault==4U) CHECK(s->source_errors>0U);
        if(fault==5U) CHECK(s->restore_skipped==1U && GPU_ALPHA==32U && GPU_CTRL==GPU_CTRL_BITBLT);
        else CHECK(s->restore_skipped==0U && GPU_ALPHA==23U && GPU_CTRL==0U);
        verify_immutable(fault==4U);
        printf("FAULT mode=%u rc=%d commands=%u pixel=%u guard=%u source=%u restore_skipped=%u\n",fault,rc,s->commands,s->pixel_errors,s->guard_errors,s->source_errors,s->restore_skipped);
    }
    /* Initial BUSY must not alter either live parameters or scratch. */
    load_atlas(argv[1]); reset_registers(); save_immutable(); GPU_STATUS=STATUS_BUSY; GPU_CTRL=0x105U;
    uint32_t scratch_hash=hash_region(PROBE_BASE,2048U);
    CHECK(gpu_precision_verify()==GPU_ETIMEOUT);
    CHECK(gpu_precision_verify_stats()->commands==0U && gpu_precision_verify_stats()->restore_skipped==1U);
    CHECK(GPU_ALPHA==23U && GPU_CTRL==0x105U && scratch_hash==hash_region(PROBE_BASE,2048U));
    verify_immutable(0);
    /* Missing one family cannot silently pass or produce an all-key test. */
    load_atlas(argv[1]); reset_registers();
    for(unsigned id=280U;id<336U;++id){
        const garden_sprite_t *s=garden_asset(id);
        uint16_t *src=(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset);
        for(unsigned i=0;i<garden_sprite_stride(s)*s->height/2U;++i) src[i]=GARDEN_KEY_COLOR;
    }
    save_immutable(); CHECK(gpu_precision_verify()==GPU_EHW);
    CHECK(gpu_precision_verify_stats()->cases==0U && command_count==0U);
    CHECK(GPU_ALPHA==23U && GPU_CTRL==0U); verify_immutable(0);
    printf("SUMMARY checks=%u successful_cases=%u high_region_sources=%u families=10 faults=5 initial_busy=1 missing_family=1 host_only=1\n",checks,success.cases,success.high_region_sources);
    return 0;
}
