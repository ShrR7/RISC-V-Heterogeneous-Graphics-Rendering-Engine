/* GPL-3.0-or-later. The CPU and hardware consume an identical ordered task.
 * This is a measurement program, not a normal-game CPU/GPU mode shortcut.
 * All formal frames clear the full 640x480 field and restore identical stars.
 * Hardware fan order is legacy tile buckets first, then the Stage9 list;
 * CPU reproduces that exact order, including overlap and key semantics.
 */
#include "report_stress.h"
#include "report_measure.h"
#include "pure_bullet_test.h"
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "gpu_alpha_ref.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_log.h"
#include "stage9_link.h"
#include "g9_memory.h"
#include "pad_hw.h"
#include <string.h>

#define R_COUNT 2093U
#define R_KEY 0xF81FU
#define R_TIMEOUT 20000000U
#define R_PIXELS ((const uint16_t *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET))
#define R_RAYS ((const PureBulletRayMeta *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_RAY_OFFSET))
typedef struct {
    uint32_t before[4];
    PureBulletParticle particles[R_COUNT];
    PureBulletPlan plans[R_COUNT];
    GpuBlitEntry legacy[27U*PURE_SCATTER_BUCKET] __attribute__((aligned(16)));
    uint32_t after[4];
} ReportStressStorage;
#define RS ((ReportStressStorage *)(uintptr_t)G9_ROUTE_ADDR)
_Static_assert(sizeof(ReportStressStorage)<G9_ROUTE_BYTES,"report pressure scratch bound");
_Static_assert(__builtin_offsetof(ReportStressStorage,legacy)%16U==0U,"batch list alignment");
_Static_assert((PURE_SCATTER_BUCKET*sizeof(GpuBlitEntry))%16U==0U,"bucket alignment");

static uint64_t r_now(void){return clint_getTime(BSP_CLINT);}
static void r_poll(void){pad_hw_poll((uint32_t)(r_now()/(BSP_CLINT_HZ/1000U)));}
static void r_publish(void)
{
    asm volatile("fence iorw,iorw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence iorw,iorw" ::: "memory");
}
static unsigned r_guards(void)
{
    unsigned bad=0U;
    for(unsigned i=0;i<4U;i++){
        bad+=RS->before[i]!=(0x52505410U+i);
        bad+=RS->after[i]!=(0x52505420U+i);
    }
    return bad;
}
static uint32_t r_crc(const uint8_t *p,unsigned n)
{
    uint32_t c=UINT32_MAX;
    for(unsigned i=0;i<n;i++){
        if(!(i&255U))r_poll();
        c^=p[i];for(unsigned b=0;b<8U;b++)c=(c>>1)^((0U-(c&1U))&0xEDB88320U);
    }
    return c^UINT32_MAX;
}
static int r_prepare(void)
{
    const PureBulletScatterHeader *h=(const PureBulletScatterHeader *)(uintptr_t)PURE_SCATTER_BASE;
    if(h->magic!=PURE_SCATTER_MAGIC || h->revision!=2U || h->total_bytes!=PURE_SCATTER_BYTES ||
       h->tile_count!=27U || h->ray_count!=91U || h->plan_count!=2730U ||
       r_crc((const uint8_t *)h,PURE_SCATTER_BYTES)!=PURE_SCATTER_BLOB_CRC)return GPU_EHW;
    for(unsigned i=0;i<4U;i++){RS->before[i]=0x52505410U+i;RS->after[i]=0x52505420U+i;}
    for(unsigned bank=0;bank<2U;bank++){
        volatile uint16_t *dst=(volatile uint16_t *)(uintptr_t)(bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR);
        for(unsigned i=0;i<G9_ATLAS_BYTES/2U;i++)dst[i]=i<27U*256U?R_PIXELS[i]:R_KEY;
    }
    unsigned n=0U;
    for(unsigned wave=0;wave<23U;wave++)for(unsigned ray=0;ray<91U;ray++)
        RS->plans[n++]=(PureBulletPlan){(uint16_t)ray,(uint16_t)(wave+1U)};
    pure_bullet_test_model_init(RS->particles,R_COUNT,RS->plans,R_RAYS,PURE_SPEED_DEFAULT_Q8);
    r_publish();
    return g9_memory_guard_check()||r_guards()?GPU_EOVERLAP:GPU_OK;
}
static int r_surfaces(unsigned backend,unsigned scene)
{
    garden_render_mode(0);garden_render_batch_mode(0);garden_ui_hud_reset();
    for(unsigned i=0;i<2U;i++){
        render_begin();garden_ui_static();
        const GardenHud h={.state="REPORT PRESSURE",.bullets=R_COUNT,.peak=R_COUNT,
            .diagnostics=1U,.cpu=backend==1U,.alpha_supported=1U,
            .alpha_enabled=scene==3U,.alpha_level=scene==3U?16U:32U};
        garden_ui_hud_cached(&h,display_back_sel());
        garden_render_box(332,632,616,48,GARDEN_UI_PANEL);
        garden_render_text(344,640,backend==1U?"CPU SOFTWARE / 2093":"HARDWARE / 2093");
        garden_render_text(344,656,scene==0U?"91 RAYS / 2 DEG / FPS VIA UART":
            scene==1U?"UNIFORM GRID / FPS VIA UART":scene==2U?"PERIMETER / FPS VIA UART":"ALPHA16 PILE / FPS VIA UART");
        int rc=render_end(R_TIMEOUT);if(rc)return rc;
    }
    garden_render_mode(backend==1U);
    return GPU_OK;
}

/* Volatile destination stores and a publication fence precede every swap.
 * No gpu_fill/blit is called in a CPU formal frame. Source is the identical
 * immutable 27-tile cache used by hardware, not a different CPU approximation. */
__attribute__((optimize("O3")))
static void r_cpu_clear(uint32_t fb)
{
    for(unsigned y=0;y<480U;y++){
        volatile uint32_t *dst=(volatile uint32_t *)(uintptr_t)(fb+(y+120U)*2560U+640U);
        for(unsigned x=0;x<320U;x++)dst[x]=0U;
        if(!(y&31U))r_poll();
    }
}
__attribute__((optimize("O3")))
static void r_cpu_sprite(uint32_t dst,unsigned tile,unsigned alpha)
{
    const uint16_t *src=(const uint16_t *)(uintptr_t)(G9_ATLAS0_ADDR+tile*512U);
    for(unsigned y=0;y<16U;y++){
        volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(dst+y*2560U);
        for(unsigned x=0;x<16U;x++){
            uint16_t v=src[y*16U+x];
            if(v!=R_KEY)out[x]=alpha==32U?v:gpu_alpha_rgb565(v,out[x],alpha);
        }
    }
}
static int r_draw_legacy(uint32_t fb,const uint16_t counts[27],unsigned cpu,unsigned alpha)
{
    (void)fb;
    for(unsigned tile=0;tile<27U;tile++){
        unsigned n=counts[tile];if(n>PURE_SCATTER_BUCKET)return GPU_EINVAL;
        const GpuBlitEntry *list=RS->legacy+tile*PURE_SCATTER_BUCKET;
        if(cpu){
            for(unsigned i=0;i<n;i++){r_cpu_sprite(list[i].dst,tile,alpha);if(!(i&31U))r_poll();}
        }else if(n){
            int rc=gpu_bitblt_batch(list,n,G9_ATLAS0_ADDR+tile*512U,16U,16U,32U,2560U,R_KEY,alpha);
            if(rc)return rc;
        }
    }
    return GPU_OK;
}
static void r_cpu_overlay(uint32_t fb,const PureBulletDesc *desc,unsigned n)
{
    for(unsigned i=0;i<n;i++){
        r_cpu_sprite(fb+(unsigned)desc[i].y*2560U+(unsigned)desc[i].x*2U,desc[i].sprite,32U);
        if(!(i&31U))r_poll();
    }
}

/* Non-fan tasks are authored in increasing tile buckets. Both renderers
 * traverse this exact order; Alpha16 overlap therefore never gets reordered.
 * Uniform and edge speed is 612/256 px per update. The pile moves inside a
 * 64x64 footprint; 'visible' means fully in bounds, not unoccluded objects. */
static int r_build_distribution(unsigned scene,uint32_t frame,uint32_t fb,PureBulletBuildResult *b)
{
    memset(b,0,sizeof(*b));uint32_t shift=(uint32_t)(((uint64_t)frame*612U)>>8),hash=0x52505431U;
    for(unsigned i=0;i<R_COUNT;i++){
        unsigned x,y,tile=i*27U/R_COUNT;
        if(scene==1U){x=(i%53U)*624U/52U;y=((i/53U)*464U/39U+shift)%465U;}
        else if(scene==2U){
            unsigned u=(i*2176U/R_COUNT+shift)%2176U;
            if(u<624U){x=u;y=0U;}
            else if(u<1088U){x=624U;y=u-624U;}
            else if(u<1712U){x=1712U-u;y=464U;}
            else{x=0U;y=2176U-u;}
        }else{x=288U+((i*17U+shift)%49U);y=208U+(i*29U)%49U;}
        if(x+16U>640U || y+16U>480U || tile>=27U)return GPU_EINVAL;
        unsigned n=b->legacy_count[tile]++;
        if(n>=PURE_SCATTER_BUCKET)return GPU_EINVAL;
        x+=320U;y+=120U;
        RS->legacy[tile*PURE_SCATTER_BUCKET+n].dst=fb+y*2560U+x*2U;
        hash=((hash<<5)|(hash>>27))^(x|(y<<16))^tile;
        b->visible++;b->changed++;
    }
    b->checksum=hash;return GPU_OK;
}

int report_stress_run(unsigned backend,unsigned scene,unsigned seconds)
{
    if(backend>2U || scene>3U || !seconds)return GPU_EINVAL;
    unsigned requested=backend;if(scene && backend==0U)backend=2U;
    unsigned saved_cpu=garden_render_is_cpu(),saved_batch=garden_render_is_batch();
    unsigned alpha=scene==3U?16U:32U,bank=0U,warm=0U,frame=0U;
    uint64_t prep_begin=r_now();int rc=stage9_link_init();if(rc)return rc;
    rc=r_prepare();if(rc)goto end;
    rc=r_surfaces(backend,scene);if(rc)goto end;
    garden_log_printf("[RPT_TASK] requested=%u backend=%u scene=%u n=2093 tile=16x16 key=0xf81f alpha=%u speed_q8=612 hz=%u logic=0 clear=full640x480 background=original_stars order=legacy_tile_then_s9 geometric_visible=1 unoccluded_not_counted=1 pile=288,208,64,64 hud_fps_frozen=1 draw_metric=raster_or_legacy_submission s9_scan_in_period=1\r\n",
        requested,backend,scene,alpha,BSP_CLINT_HZ);
    /* Preparation logging drains before warmup, not after the stable epoch. */
    for(uint64_t until=r_now()+BSP_CLINT_HZ;garden_log_stats()->queued && r_now()<until;){garden_log_pump();r_poll();}
    for(;;){
        uint64_t begin=r_now();render_begin();uint32_t fb=display_back_addr();
        if(display_check_render_target(fb)!=GPU_OK){rc=GPU_ESTATE;goto end;}
        bank^=1U;uint32_t desc_addr=bank?G9_DESC1_ADDR:G9_DESC0_ADDR;
        PureBulletDesc *desc=(PureBulletDesc *)(uintptr_t)desc_addr;
        PureBulletBuildResult built;
        if(scene==0U)rc=pure_bullet_test_model_build(RS->particles,desc,&RS->legacy[0].dst,
            R_COUNT,fb,RS->plans,R_RAYS,PURE_SPEED_DEFAULT_Q8,&built);
        else rc=r_build_distribution(scene,frame,fb,&built);
        if(rc || built.visible!=R_COUNT){rc=GPU_ESTATE;goto end;}
        uint64_t clear_begin=r_now();
        if(backend==1U)r_cpu_clear(fb);
        else{rc=gpu_fill(fb+120U*2560U+640U,640U,480U,2560U,0U);if(rc)goto end;}
        garden_ui_stars(320,120,640,480);
        uint32_t clear_ticks=(uint32_t)(r_now()-clear_begin);
        uint64_t draw_begin=r_now();
        rc=r_draw_legacy(fb,built.legacy_count,backend==1U,alpha);if(rc)goto end;
        if(backend==1U){if(scene==0U)r_cpu_overlay(fb,desc,built.s9_count);r_publish();}
        else{
            rc=gpu_wait_idle(R_TIMEOUT);if(rc)goto end;
            r_publish();
            if(backend==2U && scene==0U){
                /* Legacy fan retains the same layer order as CPU/Stage9. */
                for(unsigned i=0;i<built.s9_count;i++){
                    rc=gpu_bitblt(G9_ATLAS0_ADDR+desc[i].sprite*512U,
                        fb+(unsigned)desc[i].y*2560U+(unsigned)desc[i].x*2U,16U,16U,32U,2560U,R_KEY);
                    if(rc)goto end;
                }
            }
        }
        /* Scanout composition is asynchronous. Exclude its admission/swap
         * wait from legacy submission time; the complete presentation period
         * and service time below include that wait and the render_end fence.
         * A Stage9 value must not be called total hardware raster time. */
        uint32_t draw_ticks=(uint32_t)(r_now()-draw_begin);
        if(backend==0U){
            rc=stage9_link_present(bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR,desc_addr,built.s9_count,fb,display_back_sel());
            if(rc)goto end;
        }
        rc=render_end(R_TIMEOUT);if(rc)goto end;
        r_poll();
        if(g9_memory_guard_check() || r_guards()){rc=GPU_EOVERLAP;goto end;}
        uint64_t completed=r_now();frame++;
        if(warm<2U){
            warm++;
            if(warm==2U){
                garden_log_printf("[RPT_PREP] backend=%u scene=%u start_lo=%u start_hi=%u end_lo=%u end_hi=%u warm_frames=2 formal_start=last_completed\r\n",
                    backend,scene,(uint32_t)prep_begin,(uint32_t)(prep_begin>>32),(uint32_t)completed,(uint32_t)(completed>>32));
                rc=report_measure_begin(REPORT_KIND_STRESS,backend,scene,R_COUNT,alpha,seconds,completed);if(rc)goto end;
            }
        }else{
            rc=report_measure_frame(completed,(uint32_t)(completed-begin),draw_ticks,clear_ticks,built.visible,built.checksum);
            if(rc<0)goto end;
            if(report_measure_done()){rc=GPU_OK;break;}
        }
        garden_log_pump();
    }
end:
    if(rc && report_measure_active())report_measure_abort();
    {int stopped=stage9_link_stop();if(!rc && stopped)rc=stopped;}
    garden_render_mode(saved_cpu);garden_render_batch_mode(saved_batch);
    garden_log_printf("[RPT_STRESS_RETURN] backend=%u scene=%u rc=%d frames=%u restore_game_surfaces_required=1\r\n",backend,scene,rc,frame);
    garden_log_pump();return rc;
}
