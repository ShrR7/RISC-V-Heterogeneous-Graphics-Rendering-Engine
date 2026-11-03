/* GPL-3.0-or-later. Uniform two-degree fan with a common tunable radial speed. */
#include "pure_bullet_test.h"
#include "pad_hw.h"
#include "garden_controller.h"
_Static_assert(sizeof(PureBulletParticle)==4U,"particle ABI");
_Static_assert(sizeof(PureBulletDesc)==16U,"descriptor ABI");
static int radial_pixel(int unit,unsigned age,unsigned speed_q8)
{return (unit*(int)(age*speed_q8)+(1<<21))>>22;}
/* k30/cut6 can exceed Stage9's 2048 descriptors. Retain the verified
 * cut7 route at k<=22 and k30; use cut6 for the intervening capacities. */
static unsigned route_cut(unsigned count)
{return count>=23U*PURE_RAY_COUNT && count<=29U*PURE_RAY_COUNT?6U:7U;}
static unsigned edge_age(const PureBulletRayMeta *ray,unsigned speed_q8)
{
    int ux=(int16_t)ray->velocity,uy=(int16_t)(ray->velocity>>16);
    int ax=(int)(ray->packed&255U),ay=(int)((ray->packed>>8)&255U);
    for(unsigned age=1U;age<512U;age++){
        int x=320+radial_pixel(ux,age,speed_q8)-ax;
        int y=16+radial_pixel(uy,age,speed_q8)-ay;
        if(x<0 || y<0 || x+16>640 || y+16>480)return age;
    }
    return 0U;
}
void pure_bullet_test_model_init(PureBulletParticle *state,unsigned count,
    const PureBulletPlan *plans,const PureBulletRayMeta *rays,unsigned speed_q8)
{
    if(!state || !plans || !rays || !count || count>PURE_BULLET_MAX || count%PURE_RAY_COUNT ||
       speed_q8<PURE_SPEED_MIN_Q8 || speed_q8>PURE_SPEED_MAX_Q8)return;
    unsigned per_ray=count/PURE_RAY_COUNT;
    if(per_ray<PURE_PER_RAY_MIN || per_ray>PURE_PER_RAY_MAX)return;
    for(unsigned i=0;i<count;i++){
        unsigned ray=plans[i].ray,slot=plans[i].phase_slot;
        if(ray>=PURE_RAY_COUNT || !slot || slot>per_ray)return;
        unsigned ttl=i<PURE_RAY_COUNT?edge_age(rays+ray,speed_q8):state[ray].ttl;
        if(!ttl || ttl>511U)return;
        state[i]=(PureBulletParticle){(uint16_t)(((slot-1U)*ttl)/per_ray),(uint16_t)ttl};
    }
}
/* Keep the particle builder optimized independently of the size-tuned UI
 * and input code: submission must finish before the physical row480 gate. */
__attribute__((optimize("O3"),noinline))
int pure_bullet_test_model_build(PureBulletParticle *state,
    PureBulletDesc *s9,uint32_t *legacy_destinations,unsigned count,
    uint32_t framebuffer,const PureBulletPlan *plans,const PureBulletRayMeta *rays,
    unsigned speed_q8,
    PureBulletBuildResult *result)
{
    if(!result)return -1;
    *result=(PureBulletBuildResult){0};
    if(!state || !s9 || !legacy_destinations || !plans || !rays || count>PURE_BULLET_MAX ||
       speed_q8<PURE_SPEED_MIN_Q8 || speed_q8>PURE_SPEED_MAX_Q8)return -1;
    uint32_t crc=0x2000C0DEU;
    unsigned cut=route_cut(count);
    for(unsigned i=0;i<count;i++){
        unsigned ray=plans[i].ray;if(ray>=PURE_RAY_COUNT)return -1;
        unsigned packed=rays[ray].packed,tile=(packed>>16)&255U,family=packed>>24;
        int ax=(int)(packed&255U),ay=(int)((packed>>8)&255U);
        if(tile>=27U || family>=7U || ax>15 || ay>15 || !state[i].ttl || state[i].ttl>511U)return -1;
        unsigned old=state[i].age,age=old+1U;
        if(old>=state[i].ttl)return -1;
        if(age>=state[i].ttl){age=0U;result->respawn++;}state[i].age=(uint16_t)age;
        uint32_t unit=rays[ray].velocity;
        int px=radial_pixel((int16_t)unit,age,speed_q8);
        int py=radial_pixel((int16_t)(unit>>16),age,speed_q8);
        /* Q8 speed>=512 means >=2px radial step. At least one Q14 axis is
         * >=0.707, so rounded integer coordinates change every update;
         * natural edge respawn also changes position. */
        result->changed++;
        int lx=320+px-ax,ly=16+py-ay;
        if(lx<0 || ly<0 || lx+16>640 || ly+16>480)return -1;
        int16_t gx=(int16_t)(lx+320),gy=(int16_t)(ly+120);
        /* Lightweight ordered position signature; pixels and routing are
         * independently checked. Avoid per-particle diagnostic multiplies. */
        crc=((crc<<5)|(crc>>27))^((uint32_t)gx|((uint32_t)gy<<16));
        /* RTL19 selects Stage9 only for physical scan lines 0..479.
         * A tile touching row480 must use the GPU framebuffer on both sides
         * of that scanout boundary, including its top rows. */
        if(ray>=cut && ray<PURE_RAY_COUNT-cut && gy+16<=480){
            if(result->s9_count>=2048U)return -3;
            s9[result->s9_count++]=(PureBulletDesc){gx,gy,0,0,(uint16_t)tile,1U,0U};
        }else{
            if(result->legacy_count[tile]>=PURE_SCATTER_BUCKET)return -2;
            unsigned n=tile*PURE_SCATTER_BUCKET+result->legacy_count[tile]++;
            legacy_destinations[n]=framebuffer+(uint32_t)gy*2560U+(uint32_t)gx*2U;
        }
        result->visible++;result->family_count[family]++;
    }
    result->checksum=crc;return 0;
}

#ifndef PURE_BULLET_HOST_TEST
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_assets.h"
#include "garden_log.h"
#include "stage9_link.h"
#include "g9_memory.h"
#include "report_measure.h"
#include <string.h>

#define PURE_TIMEOUT 20000000U
#define PURE_KEY 0xF81FU
#define PURE_CLEAR_Y (480U-PURE_BULLET_SPRITE_HEIGHT)
#define TILES ((const PureBulletTileMeta *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_META_OFFSET))
#define RAYS ((const PureBulletRayMeta *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_RAY_OFFSET))
#define PLANS (STORAGE->plans)
#define SETUP_COUNTS ((uint16_t *)(uintptr_t)G9_DESC0_ADDR)
#define SETUP_REMAINDERS ((uint16_t *)(uintptr_t)(G9_DESC0_ADDR+PURE_SCATTER_RAY_COUNT*2U))
typedef struct {
    uint32_t before[4];
    PureBulletParticle particles[PURE_BULLET_STORAGE_CAPACITY];
    PureBulletPlan plans[PURE_BULLET_STORAGE_CAPACITY];
    uint16_t ray_count[33],ray_remainder[33];
    GpuBlitEntry legacy[PURE_SCATTER_TILE_COUNT*PURE_SCATTER_BUCKET] __attribute__((aligned(16)));
    uint32_t after[4];
} PureStorage;
_Static_assert(sizeof(PureStorage)<G9_ROUTE_BYTES,"pure state fits borrowed route scratch");
_Static_assert(__builtin_offsetof(PureStorage,legacy)==22000U,"aligned legacy address");
_Static_assert(PURE_SCATTER_RAY_COUNT*2U*sizeof(uint16_t)<=G9_DESC_BYTES,"stopped DESC0 setup scratch bound");
_Static_assert(__builtin_offsetof(PureStorage,legacy)%16U==0U,"pure legacy list base alignment");
_Static_assert((PURE_SCATTER_BUCKET*sizeof(GpuBlitEntry))%16U==0U,"pure legacy bucket alignment");
#define STORAGE ((PureStorage *)(uintptr_t)G9_ROUTE_ADDR)
static uint32_t generation;
static uint32_t blob_crc;
static uint64_t clock_now(void);
static unsigned test_alpha=32U,test_scene=0U,test_clear=0U;
static unsigned dirty_warm,clear_ticks_last;
typedef struct { uint32_t before[4]; uint64_t mask[2][30]; GpuFillEntry fills[1200]; uint32_t after[4]; } TestHistory;
#define TEST_HISTORY ((TestHistory *)(uintptr_t)0x00BC0000U)
_Static_assert(sizeof(TestHistory)<0x10000U,"test history DDR bound");

/* A 16-pixel cell is dirty iff it intersects a legacy sprite rectangle on
 * this exact back buffer. Stage9 pixels never enter the framebuffer. Merge
 * adjacent cells into disjoint horizontal runs, clear them all, then restore
 * the original stars. No draw is interleaved with erase operations. */
static int test_clear_field(uint32_t fb,unsigned sel)
{
    uint64_t start=clock_now();int rc=0;
    if(test_clear==1U || dirty_warm<2U){
        rc=gpu_fill(fb+120U*2560U+640U,640U,480U,2560U,0U);
        if(!rc)garden_ui_stars(320,120,640,480);
        if(dirty_warm<2U)dirty_warm++;
    }else{
        unsigned n=0U;
        for(unsigned row=0;row<30U;row++){
            uint64_t bits=TEST_HISTORY->mask[sel][row];
            for(unsigned x=0;x<40U;){
                if(!(bits&(1ULL<<x))){x++;continue;}
                unsigned first=x++;while(x<40U && (bits&(1ULL<<x)))x++;
                TEST_HISTORY->fills[n++]=(GpuFillEntry){fb+(120U+row*16U)*2560U+(320U+first*16U)*2U,(uint16_t)((x-first)*16U),16U};
            }
        }
        if(n)rc=gpu_fill_batch(TEST_HISTORY->fills,n,2560U,0U);
        if(!rc)for(unsigned i=0;i<n;i++){
            const GpuFillEntry *q=TEST_HISTORY->fills+i;unsigned off=q->dst-fb;
            garden_ui_stars((off%2560U)/2U,off/2560U,q->width,q->height);
        }
    }
    if(!rc)rc=gpu_wait_idle(PURE_TIMEOUT);
    clear_ticks_last=(uint32_t)(clock_now()-start);return rc;
}
static void test_save_history(uint32_t fb,unsigned sel,const uint16_t counts[27])
{
    for(unsigned r=0;r<30U;r++)TEST_HISTORY->mask[sel][r]=0U;
    for(unsigned tile=0;tile<27U;tile++)for(unsigned i=0;i<counts[tile];i++){
        unsigned off=STORAGE->legacy[tile*PURE_SCATTER_BUCKET+i].dst-fb;
        unsigned x=(off%2560U)/2U-320U,y=off/2560U-120U;
        unsigned x0=x/16U,x1=(x+15U)/16U,y0=y/16U,y1=(y+15U)/16U;
        for(unsigned r=y0;r<=y1;r++)for(unsigned c=x0;c<=x1;c++)TEST_HISTORY->mask[sel][r]|=1ULL<<c;
    }
}
/* Stress distributions preserve the exact same 16x16 keyed/Alpha16 tiles.
 * Whole tiles beyond scan row479 fall back, with their full extent, to the
 * legacy framebuffer. Descriptor capacity overflow also falls back; no object
 * is silently dropped. The measured backend is explicitly hybrid. */
static int test_distribution(unsigned count,uint32_t frame,uint32_t fb,PureBulletDesc *desc,PureBulletBuildResult *b)
{
    *b=(PureBulletBuildResult){0};unsigned shift=(frame*PURE_SPEED_DEFAULT_Q8)>>8;uint32_t hash=0x52505432U;
    for(unsigned i=0;i<count;i++){
        unsigned x,y,tile=i*27U/count;
        if(test_scene==1U){x=(i%53U)*624U/52U;y=((i/53U)*464U/((count+52U)/53U-1U)+shift)%465U;}
        else if(test_scene==2U){
            unsigned u=(i*2176U/count+shift)%2176U;
            if(u<624U){x=u;y=0U;}else if(u<1088U){x=624U;y=u-624U;}
            else if(u<1712U){x=1712U-u;y=464U;}else{x=0U;y=2176U-u;}
        }else{x=288U+((i*17U+shift)%49U);y=208U+(i*29U)%49U;}
        x+=320U;y+=120U;
        if(y+16U<=480U && b->s9_count<2048U)desc[b->s9_count++]=(PureBulletDesc){(int16_t)x,(int16_t)y,0,0,(uint16_t)tile,1U,0U};
        else {unsigned n=b->legacy_count[tile]++;if(n>=PURE_SCATTER_BUCKET)return -1;STORAGE->legacy[tile*PURE_SCATTER_BUCKET+n].dst=fb+y*2560U+x*2U;}
        b->visible++;b->changed++;b->family_count[0]++;hash=((hash<<5)|(hash>>27))^(x|(y<<16))^tile;
    }
    b->checksum=hash;return 0;
}

/* Setup only, with Stage9 stopped: 8..30 phase slots per ray. */
static void rebuild_plan(unsigned count)
{
    unsigned n=0U;
    for(unsigned wave=0;wave<count/PURE_RAY_COUNT;wave++)for(unsigned ray=0;ray<PURE_RAY_COUNT;ray++)
        PLANS[n++]=(PureBulletPlan){(uint16_t)ray,(uint16_t)(wave+1U)};
}

static uint64_t clock_now(void){return clint_getTime(BSP_CLINT);}
static void publish(void)
{
    asm volatile("fence iorw,iorw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence iorw,iorw" ::: "memory");
}
static void state_guards_init(void)
{
    for(unsigned i=0;i<4U;i++){
        STORAGE->before[i]=0xC200B000U+i;
        STORAGE->after[i]=0xC200A000U+i;
        TEST_HISTORY->before[i]=0xC7024400U+i;
        TEST_HISTORY->after[i]=0xC7024404U+i;
    }
}
static unsigned state_guards_bad(void)
{
    unsigned bad=0;
    for(unsigned i=0;i<4U;i++){
        bad+=STORAGE->before[i]!=0xC200B000U+i;
        bad+=STORAGE->after[i]!=0xC200A000U+i;
        bad+=TEST_HISTORY->before[i]!=0xC7024400U+i;
        bad+=TEST_HISTORY->after[i]!=0xC7024404U+i;
    }
    return bad;
}
static int draw_hud(unsigned count,uint32_t fps10,uint32_t shown10,
                    uint32_t frame,uint32_t missing,unsigned errors,unsigned ready)
{
    const GardenRenderStats *stats=garden_render_stats();
    uint32_t before=stats->errors+stats->bad_targets+stats->atlas_bounds;
    uint32_t address=display_back_addr();
    if(display_check_render_target(address)!=GPU_OK)return GPU_ESTATE;
    const GardenHud h={.state="PURE EMITTER TEST",.score=0U,.lives=0U,
        .bullets=count,.peak=count,.fps10=fps10,.errors=errors+missing,.diagnostics=1U,
        .alpha_supported=(unsigned)garden_render_alpha_supported(),
        .alpha_enabled=(unsigned)garden_render_alpha_enabled(),.alpha_level=garden_render_alpha_level()};
    garden_ui_hud_cached(&h,display_back_sel());
    if(frame==0U){
        garden_render_box(56,412,208,8,GARDEN_UI_PANEL);
        garden_render_text_style(56,412,PURE_SCATTER_TEXT_LEFT,GARDEN_TEXT_SMALL_CYAN);
        garden_render_box(332,632,616,48,GARDEN_UI_PANEL);
        garden_render_text_style(344,640,PURE_SCATTER_TEXT_FOOTER,GARDEN_TEXT_SMALL_CYAN);
        garden_render_text(344,660,PURE_SCATTER_TEXT_KEYS);
    }
    int rc=gpu_wait_idle(3000000U);if(rc)return rc;
    stats=garden_render_stats();
    if(stats->errors+stats->bad_targets+stats->atlas_bounds!=before)return GPU_EHW;
    (void)shown10;(void)ready;return 0;
}
static int prepare_surfaces(unsigned count)
{
    garden_ui_hud_reset();
    for(unsigned fb=0;fb<2U;fb++){
        render_begin();
        garden_ui_static();
        int rc=gpu_wait_idle(3000000U);if(rc)return rc;
        rc=draw_hud(count,0U,0U,0U,0U,0U,0U);if(rc)return rc;
        rc=render_end(PURE_TIMEOUT);if(rc)return rc;
    }
    return 0;
}
static uint32_t crc_at(uint32_t address,unsigned bytes)
{
    const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)address;uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<bytes;i++){
        /* Setup can outlast the 256-byte UART FIFO. Keep both controllers
         * serviced here without adding work before steady-frame admission. */
        if((i&255U)==0U)
            pad_hw_poll((uint32_t)(clock_now()/(BSP_CLINT_HZ/1000U)));
        crc^=p[i];for(unsigned bit=0;bit<8U;bit++)crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320U);
    }
    return crc^UINT32_MAX;
}
static int prepare_atlas(void)
{
    const volatile PureBulletScatterHeader *head=(const volatile PureBulletScatterHeader *)(uintptr_t)PURE_SCATTER_BASE;
    blob_crc=crc_at(PURE_SCATTER_BASE,PURE_SCATTER_BYTES);
    if(head->magic!=PURE_SCATTER_MAGIC || head->revision!=2U || head->total_bytes!=PURE_SCATTER_BYTES ||
       head->tile_count!=27U || head->ray_count!=PURE_SCATTER_RAY_COUNT || head->plan_count!=PURE_BULLET_MAX ||
       head->payload_bytes!=PURE_SCATTER_PAYLOAD_BYTES || head->payload_crc!=PURE_SCATTER_PAYLOAD_CRC ||
       blob_crc!=PURE_SCATTER_BLOB_CRC || crc_at(PURE_SCATTER_BASE+64U,PURE_SCATTER_PAYLOAD_BYTES)!=head->payload_crc)
        return GPU_EHW;
    for(unsigned tile=0;tile<PURE_SCATTER_TILE_COUNT;tile++){
        const PureBulletTileMeta *meta=TILES+tile;
        const garden_sprite_t *s=garden_asset(meta->asset);
        if(!s || s->width!=((meta->packed>>16)&255U) || s->height!=(meta->packed>>24) ||
           crc_at(GARDEN_ATLAS_ADDRESS+s->offset,s->width*s->height*2U)!=meta->source_crc ||
           crc_at(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET+tile*512U,512U)!=meta->tile_crc)return GPU_EHW;
    }
    const volatile uint16_t *pixels=(const volatile uint16_t *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET);
    for(unsigned bank=0;bank<2U;bank++){
        uint32_t address=bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR;
        volatile uint16_t *p=(volatile uint16_t *)(uintptr_t)address;
        for(unsigned i=0;i<G9_ATLAS_BYTES/2U;i++)p[i]=PURE_KEY;
        for(unsigned i=0;i<PURE_SCATTER_TILE_COUNT*256U;i++)p[i]=pixels[i];
        publish();
        for(unsigned tile=0;tile<PURE_SCATTER_TILE_COUNT;tile++)
            if(crc_at(address+tile*512U,512U)!=TILES[tile].tile_crc)return GPU_EHW;
    }
    return 0;
}
static int draw_legacy(uint32_t framebuffer,const uint16_t counts[27],unsigned count,unsigned wide_clear)
{
    unsigned clear_rows=route_cut(count)==6U?PURE_FAST_LEGACY_ROWS:PURE_UNIFORM_LEGACY_ROWS;
    if(wide_clear)clear_rows=PURE_UNIFORM_LEGACY_ROWS;
    const GardenRenderStats *stats=garden_render_stats();
    uint32_t before=stats->errors+stats->bad_targets+stats->atlas_bounds;
    uint32_t clear=framebuffer+PURE_BULLET_ORIGIN_Y*DISPLAY_STRIDE_BYTES+PURE_BULLET_ORIGIN_X*2U;
    int rc=0;
    if(!test_clear)rc=gpu_fill(clear,PURE_BULLET_WIDTH,clear_rows,
                    DISPLAY_STRIDE_BYTES,0U);
    if(rc)return rc;
    /* The identical lower strip was cleared in parallel with CPU particle
     * construction, and fenced before any HUD/legacy operation below. */
    if(!test_clear)garden_ui_stars(PURE_BULLET_ORIGIN_X,PURE_BULLET_ORIGIN_Y,PURE_BULLET_WIDTH,
                    clear_rows);
    if(!test_clear)garden_ui_stars(PURE_BULLET_ORIGIN_X,464, PURE_BULLET_WIDTH,136U);
    for(unsigned tile=0;tile<PURE_SCATTER_TILE_COUNT;tile++)for(unsigned first=0;first<counts[tile];first+=256U){
        unsigned chunk=counts[tile]-first;if(chunk>256U)chunk=256U;
        rc=gpu_bitblt_batch(STORAGE->legacy+tile*PURE_SCATTER_BUCKET+first,chunk,G9_ATLAS0_ADDR+tile*512U,
            PURE_BULLET_SPRITE_WIDTH,PURE_BULLET_SPRITE_HEIGHT,PURE_BULLET_TILE*2U,DISPLAY_STRIDE_BYTES,PURE_KEY,test_alpha);
        if(rc)return rc;
    }
    rc=gpu_wait_idle(3000000U);if(rc)return rc;
    stats=garden_render_stats();
    if(stats->errors+stats->bad_targets+stats->atlas_bounds!=before)return GPU_EHW;
    return 0;
}

/* T owns the legacy GPU and the current back surface. Clear only its fixed
 * lower strip while the CPU writes disjoint particle/descriptor/list storage.
 * This is deliberately not a general asynchronous rendering API. */
static int begin_bottom_clear(uint32_t framebuffer)
{
    if((framebuffer!=FB0_ADDR && framebuffer!=FB1_ADDR) ||
       framebuffer!=display_back_addr())return GPU_ESTATE;
    int rc=gpu_wait_idle(3000000U);if(rc)return rc;
    GPU_DST_ADDR=framebuffer+464U*DISPLAY_STRIDE_BYTES+PURE_BULLET_ORIGIN_X*2U;
    GPU_WIDTH=PURE_BULLET_WIDTH;GPU_HEIGHT=136U;
    GPU_DST_STRIDE=DISPLAY_STRIDE_BYTES;GPU_COLOR=0U;
    asm volatile("fence iorw,iorw" ::: "memory");
    GPU_CTRL=0U;GPU_CTRL=GPU_CTRL_FILL;
    return 0;
}
static int finish_bottom_clear(void)
{
    int rc=gpu_wait_idle(3000000U);if(rc)return rc;
    uint32_t status=GPU_STATUS;
    if((status&STATUS_ERROR) || !(status&STATUS_DONE))return GPU_EHW;
    publish();return 0;
}
static int failure(int rc,uint32_t frame,unsigned count)
{
    if(report_measure_active())report_measure_abort();
    report_measure_deferred(0U);stage9_link_alpha(32U);
    garden_log_printf(PURE_SCATTER_TEXT_FAIL,generation,frame,count,rc);
    garden_log_pump();return rc<0?rc:GPU_EHW;
}
static void start_report(unsigned count,uint32_t frame,unsigned warmup,unsigned speed_q8)
{
    unsigned life_min=511U,life_max=0U;
    for(unsigned ray=0;ray<PURE_RAY_COUNT;ray++){
        unsigned life=STORAGE->particles[ray].ttl;
        if(life<life_min)life_min=life;
        if(life>life_max)life_max=life;
    }
    garden_log_printf(PURE_SCATTER_TEXT_START,generation,count,speed_q8,count/PURE_RAY_COUNT,
        count/PURE_RAY_COUNT,route_cut(count)*2U,life_min,life_max,warmup,blob_crc,GARDEN_ATLAS_CRC32,frame);
}
static unsigned stream_bad(const Stage9LinkStats *v,const Stage9LinkStats *b)
{
    return v->missing_px!=b->missing_px || v->missing_lines!=b->missing_lines ||
           v->rejected!=b->rejected || v->skipped!=b->skipped || v->errors>b->errors;
}
static void warmup_report(unsigned count,unsigned speed_q8)
{
    generation++;
    garden_log_printf(PURE_SCATTER_TEXT_WARMUP,generation,count,speed_q8,count/PURE_RAY_COUNT,
        (unsigned)PLANS[count-1U].phase_slot);
}

/* A report belongs to the completed two-second window, but formatting it
 * immediately after row719 can push the next frame past the row480 deadline.
 * Keep its exact snapshot and emit it after the next present has been armed.
 * render_end then measures the full physical frame including this work. */
typedef struct {
    uint32_t args[41];
    uint32_t max_ticks,over60,over3;
    uint64_t build_ticks,legacy_ticks,present_ticks;
    uint32_t sample_updates;
} PendingReport;
static void emit_pending_report(PendingReport *report)
{
    uint32_t *a=report->args;
    a[30]=(uint32_t)(report->build_ticks*1000000ULL/BSP_CLINT_HZ/report->sample_updates);
    a[31]=(uint32_t)(report->legacy_ticks*1000000ULL/BSP_CLINT_HZ/report->sample_updates);
    a[32]=(uint32_t)(report->present_ticks*1000000ULL/BSP_CLINT_HZ/report->sample_updates);
    garden_log_printf(PURE_SCATTER_TEXT_REPORT,
        a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],
        a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],
        a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],
        a[24],a[25],a[26],a[27],a[28],a[29],a[30],a[31],
        a[32],a[33],a[34],a[35],a[36],a[37],a[38],a[39],
        a[40]);
    garden_log_printf(PURE_SCATTER_TEXT_PERIOD,a[0],a[1],a[2],a[3],
        report->max_ticks,report->over60,report->over3,(unsigned)BSP_CLINT_HZ);
    garden_log_pump();
}

int t_compare_run(void);
int pure_bullet_test_run(void) { return t_compare_run(); }
#endif
