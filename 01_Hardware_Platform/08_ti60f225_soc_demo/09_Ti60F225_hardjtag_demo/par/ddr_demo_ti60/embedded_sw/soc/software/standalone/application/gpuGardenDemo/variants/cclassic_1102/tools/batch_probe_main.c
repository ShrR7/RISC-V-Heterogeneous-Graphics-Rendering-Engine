/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent RTL12 comparison firmware, never linked into the game.
 * Only the BACK framebuffer and reserved DDR scratch are written.
 */
#include <stdint.h>
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_verify.h"
#include "gpu_precision_verify.h"
#include "gpu_display.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_log.h"
#include "garden_core.h"

#define REF_ADDR 0x00800000U
#define LIST_ADDR 0x0067C000U
#define RENDER_LIST 0x0067D000U
#define GUARD 0x65A49F23U
#define PIXELS (DISPLAY_WIDTH*DISPLAY_HEIGHT)
#define ROUNDS 6U
static GardenDraw draws[128];
static uint32_t crc_table[256];
static unsigned failures;
void trap_entry(void);
void trap(void) {
    bsp_printf("[BATCH_FATAL] trap mcause=0x%x mepc=0x%x\r\n",(uint32_t)csr_read(mcause),(uint32_t)csr_read(mepc));
    while(1) {}
}
static unsigned tx_free(void) { return uart_writeAvailability(BSP_UART_TERMINAL); }
static void tx_put(char c) { uart_write(BSP_UART_TERMINAL,c); }
static void fence_ddr(void) {
    asm volatile("fence iorw,iorw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence iorw,iorw" ::: "memory");
}
static uint32_t clock32(void) { return clint_getTimeLow(BSP_CLINT); }
static uint32_t us(uint32_t ticks) { return (uint32_t)((uint64_t)ticks*1000000U/BSP_CLINT_HZ); }
static void fatal(const char *s) {
    garden_log_printf("[BATCH_FATAL] %s status=0x%x\r\n",s,gpu_last_status());
    while(1) {}
}
static void crc_init(void) {
    for(unsigned i=0;i<256;i++) {
        uint32_t c=i;
        for(unsigned b=0;b<8;b++) c=(c>>1)^((0U-(c&1U))&0xEDB88320U);
        crc_table[i]=c;
    }
}
static uint32_t crc_memory(uint32_t addr,unsigned bytes) {
    const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)addr;
    uint32_t c=0xFFFFFFFFU;
    for(unsigned i=0;i<bytes;i++) c=crc_table[(c^p[i])&255U]^(c>>8);
    return c^0xFFFFFFFFU;
}
static void guard_write(uint32_t addr) {
    volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)addr;
    for(unsigned i=0;i<4;i++) p[i]=GUARD^i;
}
static unsigned guard_check(uint32_t addr) {
    const volatile uint32_t *p=(const volatile uint32_t *)(uintptr_t)addr;
    unsigned n=0;
    for(unsigned i=0;i<4;i++) n+=p[i]!=(GUARD^i);
    return n;
}
static unsigned guards(void) {
    return guard_check(REF_ADDR-16U)+guard_check(REF_ADDR+FB_SIZE_BYTES)+
        guard_check(LIST_ADDR-16U)+guard_check(LIST_ADDR+2048U)+
        guard_check(RENDER_LIST-16U)+guard_check(RENDER_LIST+2048U);
}
static void clear_back(void) {
    uint32_t base=display_back_addr();
    for(unsigned row=0;row<DISPLAY_HEIGHT;row+=16U)
        if(gpu_fill(base+row*DISPLAY_STRIDE_BYTES,DISPLAY_WIDTH,16U,DISPLAY_STRIDE_BYTES,0x0421U)!=GPU_OK) fatal("clear");
}
static unsigned scene_items(unsigned scene) { return scene==3U?48U:scene==4U?8U:128U; }
static void make_draws(unsigned scene) {
    unsigned count=scene_items(scene);
    for(unsigned i=0;i<count;i++) {
        unsigned asset=garden_bullet_id(GARDEN_BULLET_DART,1U,0U),alpha=32U;
        int x=24+(int)(i%16U)*39,y=36+(int)(i/16U)*54;
        if(scene==2U) asset=garden_bullet_id(GARDEN_BULLET_DART,1U+i%5U,(i%8U)*128U);
        if(scene==3U) {
            asset=garden_bullet_id(GARDEN_BULLET_DART,1U+(i/6U)%3U,0U);
            alpha=8U*(1U+(i/6U)%3U);
            x=240+(int)(i%8U)*5; y=180+(int)(i/8U)*5;
        }
        draws[i]=(GardenDraw){(int16_t)x,(int16_t)y,(uint16_t)asset,(uint8_t)alpha};
    }
}
static uint32_t fill_dst(unsigned i) {
    return display_back_addr()+(GARDEN_Y0+20U+(i/16U)*54U)*DISPLAY_STRIDE_BYTES+
        (GARDEN_X0+12U+(i%16U)*39U)*2U;
}
static uint32_t large_dst(unsigned i) {
    return display_back_addr()+(GARDEN_Y0+25U+(i/4U)*155U)*DISPLAY_STRIDE_BYTES+
        (GARDEN_X0+12U+(i%4U)*145U)*2U;
}
static int direct(unsigned scene,unsigned batch_path) {
    if(scene==0U) {
        if(batch_path) {
            volatile GpuFillEntry *list=(volatile GpuFillEntry *)(uintptr_t)LIST_ADDR;
            for(unsigned i=0;i<128U;i++)
                list[i]=(GpuFillEntry){fill_dst(i),(uint16_t)(12U+i%19U),(uint16_t)(3U+i%23U)};
            return gpu_fill_batch((const GpuFillEntry *)(uintptr_t)LIST_ADDR,128U,DISPLAY_STRIDE_BYTES,0xA55AU);
        }
        for(unsigned i=0;i<128U;i++) {
            int rc=gpu_fill(fill_dst(i),12U+i%19U,3U+i%23U,DISPLAY_STRIDE_BYTES,0xA55AU);
            if(rc!=GPU_OK) return rc;
        }
    } else {
        const garden_sprite_t *s=garden_asset(GARDEN_ASSET_BOSS1);
        uint32_t source=ASSET_BASE_ADDR+s->offset;
        if(batch_path) {
            volatile GpuBlitEntry *list=(volatile GpuBlitEntry *)(uintptr_t)LIST_ADDR;
            for(unsigned i=0;i<8U;i++) list[i].dst=large_dst(i);
            return gpu_bitblt_batch((const GpuBlitEntry *)(uintptr_t)LIST_ADDR,8U,source,s->width,s->height,garden_sprite_stride(s),DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR,32U);
        }
        for(unsigned i=0;i<8U;i++) {
            int rc=gpu_bitblt(source,large_dst(i),s->width,s->height,garden_sprite_stride(s),DISPLAY_STRIDE_BYTES,GARDEN_KEY_COLOR);
            if(rc!=GPU_OK) return rc;
        }
    }
    return GPU_OK;
}
static uint32_t prepare(unsigned scene,unsigned batch_path) {
    garden_render_init();
    garden_render_alpha_support(1);
    garden_render_batch_mode(batch_path);
    render_begin();
    clear_back();
    if(scene!=0U && scene!=4U) {
        make_draws(scene);
        if(garden_render_frame(draws,scene_items(scene))!=GPU_OK) fatal("warmup");
    }
    fence_ddr();
    return crc_memory(display_back_addr(),FB_SIZE_BYTES);
}
static uint32_t copy_reference(void) {
    volatile uint32_t *out=(volatile uint32_t *)(uintptr_t)REF_ADDR;
    const volatile uint32_t *in=(const volatile uint32_t *)(uintptr_t)display_back_addr();
    for(unsigned i=0;i<FB_SIZE_BYTES/4U;i++) out[i]=in[i];
    fence_ddr();
    return crc_memory(REF_ADDR,FB_SIZE_BYTES);
}
static unsigned compare_reference(void) {
    const volatile uint16_t *ref=(const volatile uint16_t *)(uintptr_t)REF_ADDR;
    const volatile uint16_t *got=(const volatile uint16_t *)(uintptr_t)display_back_addr();
    unsigned n=0;
    for(unsigned i=0;i<PIXELS;i++) n+=ref[i]!=got[i];
    return n;
}
typedef struct { uint32_t initial,crc,commands,us; unsigned errors; } Sample;
static Sample run_sample(unsigned scene,unsigned batch_path) {
    Sample out={0};
    out.initial=prepare(scene,batch_path);
    GardenRenderStats before=*garden_render_stats();
    uint32_t t=clock32();
    int rc=scene==0U || scene==4U ? direct(scene,batch_path):garden_render_frame(draws,scene_items(scene));
    uint32_t elapsed=clock32()-t;
    GardenRenderStats after=*garden_render_stats();
    out.us=us(elapsed);
    out.commands=scene==0U || scene==4U ? (batch_path?1U:scene_items(scene)):after.commands-before.commands;
    out.errors=rc!=GPU_OK || after.errors!=before.errors || after.bad_targets!=before.bad_targets || after.atlas_bounds!=before.atlas_bounds;
    fence_ddr();
    out.crc=crc_memory(display_back_addr(),FB_SIZE_BYTES);
    return out;
}
int main(void) {
    bsp_init();csr_write(mtvec,trap_entry);
    garden_log_bind(tx_free,tx_put);garden_log_set_async(0U);crc_init();
    if(GPU_ID!=GPU_MAGIC_ID) fatal("GPU1");
    if(display_init()!=GPU_OK || display_probe()!=GPU_OK) fatal("display");
    unsigned pending_polls=0;
    while(display_swap_pending() && pending_polls<20000000U) pending_polls++;
    if(display_swap_pending()) fatal("pending swap" );
    if(display_init()!=GPU_OK) fatal("display init" );
    if(crc_memory(ASSET_BASE_ADDR,GARDEN_ATLAS_BYTES)!=GARDEN_ATLAS_CRC32) fatal("atlas");
    render_begin();
    if(gpu_alpha_verify()!=GPU_OK || gpu_precision_verify()!=GPU_OK) fatal("pixel probes");
    guard_write(REF_ADDR-16U);guard_write(REF_ADDR+FB_SIZE_BYTES);
    guard_write(LIST_ADDR-16U);guard_write(LIST_ADDR+2048U);
    guard_write(RENDER_LIST-16U);guard_write(RENDER_LIST+2048U);fence_ddr();
    uint32_t front=display_front_addr(),front_crc=crc_memory(front,FB_SIZE_BYTES);
    const garden_sprite_t *large=garden_asset(GARDEN_ASSET_BOSS1);
    unsigned large_bytes=((large->offset&15U)+large->width*2U+15U)/16U*16U*large->height;
    if(large_bytes<=4096U) fatal("fallback source must exceed cache");
    garden_log_printf("[BATCH_PROBE] atlas=0x%x ref=0x%x list=0x%x render_list=0x%x fallback_source_bytes=%u front=0x%x\r\n",GARDEN_ATLAS_CRC32,REF_ADDR,LIST_ADDR,RENDER_LIST,large_bytes,front);
    garden_log_printf("[BATCH_BENCH_CTRL] version=1 rounds=6 parity=AB_BA compare=full_pixels timing=submit_wait_only init=outside_timing hash=outside_timing present=excluded seed=197812 scenes=fill_grid,bullets_same128,bullets_mixed128,alpha_overlap,cache_fallback\r\n");
    const char *names[]={"fill_grid","bullets_same128","bullets_mixed128","alpha_overlap","cache_fallback"};
    for(unsigned scene=0;scene<5U;scene++) for(unsigned round=0;round<ROUNDS;round++) {
        Sample samples[2];unsigned diff=0,guard=0;
        for(unsigned slot=0;slot<2U;slot++) {
            unsigned path=slot^(round&1U);
            samples[path]=run_sample(scene,path);
            Sample *s=&samples[path];
            guard+=guards();
            garden_log_printf("[BATCH_SAMPLE] scene=%s round=%u order=%s path=%s us=%u commands=%u items=%u pixels=%u crc=0x%x initial_crc=0x%x errors=%u\r\n",
                names[scene],round,round&1U?"BA":"AB",path?"batch":"single",s->us,s->commands,scene_items(scene),PIXELS,s->crc,s->initial,s->errors);
            if(!slot) {
                if(copy_reference()!=s->crc) guard++;
            } else diff=compare_reference();
        }
        if(samples[0].initial!=samples[1].initial) guard++;
        garden_log_printf("[BATCH_DIFF] scene=%s round=%u diff=%u compared=%u initial_crc=0x%x guard=%u\r\n",names[scene],round,diff,PIXELS,samples[0].initial,guard);
        failures+=!!(diff || guard || samples[0].errors || samples[1].errors);
    }
    fence_ddr();
    unsigned final_guard=guards()+(crc_memory(front,FB_SIZE_BYTES)!=front_crc)+
        (crc_memory(ASSET_BASE_ADDR,GARDEN_ATLAS_BYTES)!=GARDEN_ATLAS_CRC32);
    failures+=!!final_guard;
    garden_log_printf("[BATCH_PROBE_END] front_unchanged=%u atlas_unchanged=%u guard=%u\r\n",crc_memory(front,FB_SIZE_BYTES)==front_crc,crc_memory(ASSET_BASE_ADDR,GARDEN_ATLAS_BYTES)==GARDEN_ATLAS_CRC32,final_guard);
    garden_log_printf("[BATCH_BENCH_DONE] samples=60 pairs=30 failures=%u\r\n",failures);
    while(1) {}
}

