/* GPL-3.0-or-later. Uniform two-degree fan with a common tunable radial speed. */
#include "pure_bullet_test.h"
#include "pad_hw.h"
#include "garden_controller.h"
#include "triple_boss.h"
#include "pressure_player.h"
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
    union {
        struct { PureBulletParticle particles[PURE_BULLET_MAX]; PureBulletPlan plans[PURE_BULLET_MAX]; };
        /* B has an analytic ray plan, so borrow the unused T plan bytes for
         * extra particles without relocating any GPU destination lists. */
        PureBulletParticle triple_particles[2U*PURE_BULLET_MAX];
    };
    uint16_t ray_count[33],ray_remainder[33];
    GpuBlitEntry legacy[PURE_SCATTER_TILE_COUNT*PURE_SCATTER_BUCKET] __attribute__((aligned(16)));
    uint32_t after[4];
} PureStorage;
_Static_assert(sizeof(PureStorage)<G9_ROUTE_BYTES,"pure state fits borrowed route scratch");
_Static_assert(__builtin_offsetof(PureStorage,legacy)==22000U,"aligned legacy address");
_Static_assert(TRIPLE_BULLET_MAX<=2U*PURE_BULLET_MAX,"triple particles fit stopped T plan storage");
_Static_assert(PURE_SCATTER_RAY_COUNT*2U*sizeof(uint16_t)<=G9_DESC_BYTES,"stopped DESC0 setup scratch bound");
_Static_assert(__builtin_offsetof(PureStorage,legacy)%16U==0U,"pure legacy list base alignment");
_Static_assert((PURE_SCATTER_BUCKET*sizeof(GpuBlitEntry))%16U==0U,"pure legacy bucket alignment");
#define STORAGE ((PureStorage *)(uintptr_t)G9_ROUTE_ADDR)
static unsigned triple_mode;
static uint32_t generation;
static uint32_t blob_crc;

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
    }
}
static unsigned state_guards_bad(void)
{
    unsigned bad=0;
    for(unsigned i=0;i<4U;i++){
        bad+=STORAGE->before[i]!=0xC200B000U+i;
        bad+=STORAGE->after[i]!=0xC200A000U+i;
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
    const GardenHud h={.state=triple_mode?"TRIPLE BOSS STRESS":"PURE EMITTER TEST",.score=0U,.lives=0U,
        .bullets=count,.peak=count,.fps10=fps10,.errors=errors+missing,.diagnostics=1U,
        .alpha_supported=(unsigned)garden_render_alpha_supported(),
        .alpha_enabled=(unsigned)garden_render_alpha_enabled(),.alpha_level=garden_render_alpha_level()};
    garden_ui_hud_cached(&h,display_back_sel());
    if(frame==0U){
        garden_render_box(56,412,208,8,GARDEN_UI_PANEL);
        garden_render_text_style(56,412,PURE_SCATTER_TEXT_LEFT,GARDEN_TEXT_SMALL_CYAN);
        garden_render_box(332,632,616,48,GARDEN_UI_PANEL);
        garden_render_text_style(344,640,triple_mode?"THREE BOSSES / LIVE BULLET STRESS":PURE_SCATTER_TEXT_FOOTER,GARDEN_TEXT_SMALL_CYAN);
        garden_render_text(344,660,triple_mode?"B/X EXIT / MINUS RESTART":PURE_SCATTER_TEXT_KEYS);
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
        if(triple_mode){int boss_rc=triple_boss_backdrop();if(boss_rc)return boss_rc;}
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
    if(!triple_mode)rc=gpu_fill(clear,PURE_BULLET_WIDTH,clear_rows,
                    DISPLAY_STRIDE_BYTES,0U);
    if(rc)return rc;
    /* The identical lower strip was cleared in parallel with CPU particle
     * construction, and fenced before any HUD/legacy operation below. */
    if(!triple_mode)garden_ui_stars(PURE_BULLET_ORIGIN_X,PURE_BULLET_ORIGIN_Y,PURE_BULLET_WIDTH,
                    clear_rows);
    garden_ui_stars(PURE_BULLET_ORIGIN_X,464, PURE_BULLET_WIDTH,136U);
    for(unsigned tile=0;tile<PURE_SCATTER_TILE_COUNT;tile++)for(unsigned first=0;first<counts[tile];first+=256U){
        unsigned chunk=counts[tile]-first;if(chunk>256U)chunk=256U;
        rc=gpu_bitblt_batch(STORAGE->legacy+tile*PURE_SCATTER_BUCKET+first,chunk,G9_ATLAS0_ADDR+tile*512U,
            PURE_BULLET_SPRITE_WIDTH,PURE_BULLET_SPRITE_HEIGHT,PURE_BULLET_TILE*2U,DISPLAY_STRIDE_BYTES,PURE_KEY,32U);
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
    garden_log_printf(PURE_SCATTER_TEXT_FAIL,generation,frame,count,rc);
    garden_log_pump();return rc<0?rc:GPU_EHW;
}
static void start_report(unsigned count,uint32_t frame,unsigned warmup,unsigned speed_q8)
{
    if(triple_mode){
        garden_log_printf("[TRIPLE_READY] generation=%u count=%u speed_q8=%u warmup=%u atlas=0x%08x frame=%u fan_deg=60 player_move=1 collision=0\r\n",generation,count,speed_q8,warmup,GARDEN_ATLAS_CRC32,frame);
        return;
    }
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
    if(triple_mode){
        garden_log_printf("[TRIPLE_WARMUP] generation=%u count=%u speed_q8=%u bosses=3\r\n",generation,count,speed_q8);
        return;
    }
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

static int pure_run(unsigned three)
{
    triple_mode=three;
    unsigned saved_cpu=(unsigned)garden_render_is_cpu();
    const unsigned saved_batch=(unsigned)garden_render_is_batch();
    unsigned count=three?2730U:2093U,active=count,ready=0U,bank=0U,frame=0U,errors=0U,warmup_start=0U;
    unsigned speed_q8=PURE_SPEED_DEFAULT_Q8,ready_prep=0U,prep_over3=0U,hud_mask=0U,start_pending=0U,wide_clear=0U;
    uint32_t prep_max_ticks=0U,command_to_first_ticks=0U,previous_to_first_ticks=0U;
    uint32_t interval1_ticks=0U,interval2_ticks=0U;
    uint64_t prep_last_completed=0U,switch_start=clock_now(),switch_prev_completed=0U;
    int rc=stage9_link_stop();if(rc)return failure(rc,frame,count);
    garden_render_mode(0);garden_render_batch_mode(0);
    rc=prepare_atlas();if(rc)return failure(rc,frame,count);
    rc=prepare_surfaces(count);if(rc)return failure(rc,frame,count);
    state_guards_init();if(three)triple_boss_init(STORAGE->triple_particles,count,RAYS,speed_q8);else {rebuild_plan(count);pure_bullet_test_model_init(STORAGE->particles,count,PLANS,RAYS,speed_q8);}
    Stage9LinkStats base,current;stage9_link_snapshot(&base);
    uint32_t previous_shown=base.displayed,previous_engine=base.engine;
    uint32_t updates=0U,min_changed=count,min_visible=count,max_visible=0U,checksum=0U,respawn=0U,max_bucket=0U;
    uint64_t window_start=clock_now(),build_ticks=0U,present_ticks=0U,legacy_ticks=0U;
    unsigned hud_dirty[2]={0U,0U};uint32_t fps10=0U,shown10=0U;
    unsigned command_active=0U,command_value=0U;
    unsigned paused=0U;
    unsigned per_ray_max=three?TRIPLE_PER_RAY_MAX:PURE_PER_RAY_MAX;
    uint64_t last_completed=0U;uint32_t period_max=0U,period_over=0U,period_over3=0U;
    PendingReport pending={0};unsigned report_pending=0U;
    if(three){rc=pressure_player_init();if(rc)return failure(rc,frame,count);}
    warmup_report(count,speed_q8);
    for(;;){
        unsigned selected_speed=speed_q8,selected_count=count,leave=0U,exact=0U;
        /* Input is drained during the preceding swap wait, after Stage9's
         * admission deadline. X returns unchanged; Y returns and switches the
         * normal game's backend. The T scene itself remains a GPU test. */
        uint16_t pad_events=pad_hw_take_events((uint32_t)(clock_now()/(BSP_CLINT_HZ/1000U)));
        if(pad_events&(GARDEN_CONTROLLER_TEST|GARDEN_CONTROLLER_BOSS))leave=1U;
        if(pad_events&GARDEN_INPUT_RESTART)leave=2U;
        if(pad_events&GARDEN_CONTROLLER_CPU){saved_cpu^=1U;if(leave!=2U)leave=1U;}
        while(uart_readOccupancy(BSP_UART_TERMINAL)){
            char key=uart_read(BSP_UART_TERMINAL);if(key>='A'&&key<='Z')key+='a'-'A';
            if((key=='t' || key=='h' || key=='b') && leave!=2U)leave=1U;
            if(key=='r' || key=='-')leave=2U;
            if(key=='v'){command_active=1U;command_value=0U;continue;}
            if(key=='c'){command_active=3U;command_value=0U;continue;}
            if(command_active){
                if(key>='0'&&key<='9'){
                    if(command_active==1U || command_active==3U){
                        command_value=command_value*10U+(unsigned)(key-'0');
                        if(command_value>(command_active==1U?PURE_SPEED_MAX_Q8:per_ray_max))
                            command_active=command_active==1U?2U:4U;
                    }
                }else if(key=='\n'){
                    if(command_active==1U && command_value>=PURE_SPEED_MIN_Q8){selected_speed=command_value;exact=1U;}
                    if(command_active==3U && command_value>=PURE_PER_RAY_MIN && command_value<=per_ray_max){
                        selected_count=command_value*PURE_RAY_COUNT;exact=1U;
                    }
                    command_active=0U;
                }else if(key!='\r')command_active=command_active==3U?4U:2U;
                continue;
            }
        }
        if(report_pending && (leave || selected_speed!=speed_q8 || selected_count!=count || exact)){
            /* A command changes generation before the next present. Preserve
             * the completed old-generation window and its log order. */
            emit_pending_report(&pending);report_pending=0U;
        }
        if(leave){
            rc=stage9_link_stop();if(rc)return failure(rc,frame,count);
            garden_render_mode((int)saved_cpu);garden_render_batch_mode((int)saved_batch);
            garden_log_printf(PURE_SCATTER_TEXT_EXIT,generation,frame,count,errors);
            garden_log_pump();return leave==2U?2:0;
        }
        if(three && (pad_events&GARDEN_INPUT_PAUSE))paused^=1U;
        if(paused){pad_hw_poll((uint32_t)(clock_now()/(BSP_CLINT_HZ/1000U)));garden_log_pump();continue;}
        if(selected_speed!=speed_q8 || selected_count!=count || exact){
            switch_start=clock_now();switch_prev_completed=last_completed;
            stage9_link_snapshot(&current);
            if(stream_bad(&current,&base))return failure(STAGE9_LINK_EFRAME,frame,count);
            base=current;
            speed_q8=selected_speed;count=selected_count;
            if(count!=active)wide_clear=2U;
            if(three)triple_boss_init(STORAGE->triple_particles,count,RAYS,speed_q8);else {rebuild_plan(count);pure_bullet_test_model_init(STORAGE->particles,count,PLANS,RAYS,speed_q8);}
            active=count;ready=ready_prep=prep_over3=prep_max_ticks=start_pending=0U;
            prep_last_completed=0U;command_to_first_ticks=previous_to_first_ticks=interval1_ticks=interval2_ticks=0U;
            hud_mask=0U;warmup_start=frame;fps10=shown10=0U;
            updates=0U;min_changed=min_visible=count;max_visible=0U;window_start=clock_now();build_ticks=present_ticks=legacy_ticks=0U;
            respawn=max_bucket=0U;last_completed=0U;period_max=period_over=period_over3=0U;
            previous_shown=current.displayed;previous_engine=current.engine;
            hud_dirty[0]=hud_dirty[1]=1U;warmup_report(count,speed_q8);
        }
        uint64_t begin=clock_now();render_begin();unsigned sel=display_back_sel();
        if(three){rc=pressure_player_restore(display_back_addr(),sel);if(rc)return failure(rc,frame,count);}
        rc=begin_bottom_clear(display_back_addr());if(rc)return failure(rc,frame,count);
        bank^=1U;
        uint32_t desc_address=bank?G9_DESC1_ADDR:G9_DESC0_ADDR;
        uint32_t atlas_address=bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR;
        PureBulletDesc *desc=(PureBulletDesc *)(uintptr_t)desc_address;
        PureBulletBuildResult built;
        int modeled=three?triple_boss_build(STORAGE->triple_particles,desc,&STORAGE->legacy[0].dst,active,display_back_addr(),RAYS,speed_q8,&built):pure_bullet_test_model_build(STORAGE->particles,desc,&STORAGE->legacy[0].dst,
            active,display_back_addr(),PLANS,RAYS,speed_q8,&built);
        if(modeled)return failure(modeled==-3?-2048:GPU_EINVAL,frame,count);
        unsigned changed=built.changed,visible=built.visible;
        checksum=built.checksum;
        if(changed!=active || visible!=active)return failure(GPU_ESTATE,frame,count);
        if(changed<min_changed)min_changed=changed;
        if(visible<min_visible)min_visible=visible;
        if(visible>max_visible)max_visible=visible;
        unsigned s9_count=built.s9_count;
        unsigned legacy_count=0U,family_count=0U;
        for(unsigned tile=0;tile<PURE_SCATTER_TILE_COUNT;tile++){
            unsigned n=built.legacy_count[tile];
            if(n>PURE_SCATTER_BUCKET)return failure(GPU_ESTATE,frame,count);
            if(n>max_bucket)max_bucket=n;
            legacy_count+=n;
        }
        for(unsigned family=0;family<7U;family++)family_count+=built.family_count[family];
        if(s9_count+legacy_count!=active || family_count!=active)return failure(GPU_ESTATE,frame,count);
        respawn+=built.respawn;
        rc=finish_bottom_clear();if(rc)return failure(rc,frame,count);
        unsigned had_hud=0U;
        if(hud_dirty[sel]){
            rc=draw_hud(visible,fps10,shown10,frame,0U,errors,ready);if(rc)return failure(rc,frame,count);hud_dirty[sel]=0U;
            had_hud=1U;
        }
        build_ticks+=clock_now()-begin;begin=clock_now();
        rc=draw_legacy(display_back_addr(),built.legacy_count,active,wide_clear);if(rc)return failure(rc,frame,count);
        if(three){rc=pressure_player_draw(display_back_addr(),sel,(uint32_t)(clock_now()/(BSP_CLINT_HZ/1000U)));if(rc)return failure(rc,frame,count);}
        if(wide_clear)wide_clear--;
        legacy_ticks+=clock_now()-begin;begin=clock_now();
        rc=stage9_link_present(atlas_address,desc_address,s9_count,display_back_addr(),sel);
        if(rc)return failure(rc,frame,count);
        if(start_pending){
            /* The row480 admission deadline has passed. Serialize START
             * while render_end waits for the same physical frame to finish.
             * The PREP completion timestamp remains last_completed, so
             * PERIOD still measures this entire transition, including log IO. */
            start_report(count,frame,frame-warmup_start,speed_q8);
            garden_log_pump();start_pending=0U;
        }
        if(report_pending){
            emit_pending_report(&pending);report_pending=0U;
        }
        /* Poll after present is armed: avoid moving the row480 deadline by
         * decoding a FIFO full of controller frames before submission. */
        pad_hw_poll((uint32_t)(clock_now()/(BSP_CLINT_HZ/1000U)));
        rc=render_end(PURE_TIMEOUT);if(rc)return failure(rc,frame,count);
        present_ticks+=clock_now()-begin;
        unsigned guard=g9_memory_guard_check()+state_guards_bad();
        if(guard)return failure(GPU_EOVERLAP,frame,count);
        uint64_t completed=clock_now();
        if(ready && last_completed){
            uint64_t delta=completed-last_completed;
            if(delta>period_max)period_max=(uint32_t)delta;
            period_over+=delta>(uint64_t)BSP_CLINT_HZ/60U;
            period_over3+=delta>((uint64_t)BSP_CLINT_HZ*103U/6000U);
        }
        last_completed=completed;
        frame++;garden_log_pump();
        if(!ready){
            if(!ready_prep){
                /* The final HUD was drawn into both buffers during setup.
                 * Keep the first full frame and both following intervals
                 * observable; never force a HUD repaint in PREP. */
                command_to_first_ticks=(uint32_t)(completed-switch_start);
                previous_to_first_ticks=switch_prev_completed?(uint32_t)(completed-switch_prev_completed):0U;
                hud_mask=had_hud;
                ready_prep=1U;prep_last_completed=completed;continue;
            }
            uint64_t prep_delta=completed-prep_last_completed;
            if(prep_delta>prep_max_ticks)prep_max_ticks=(uint32_t)prep_delta;
            prep_over3+=prep_delta>((uint64_t)BSP_CLINT_HZ*103U/6000U);
            if(ready_prep==1U){interval1_ticks=(uint32_t)prep_delta;hud_mask|=had_hud<<1;}
            else{interval2_ticks=(uint32_t)prep_delta;hud_mask|=had_hud<<2;}
            prep_last_completed=completed;
            if(ready_prep++<2U)continue;
            garden_log_printf(PURE_SCATTER_TEXT_PREP,
                generation,speed_q8,count/PURE_RAY_COUNT,command_to_first_ticks,previous_to_first_ticks,
                interval1_ticks,interval2_ticks,prep_max_ticks,prep_over3,hud_mask);
            garden_log_pump();
            stage9_link_snapshot(&current);
            if(stream_bad(&current,&base))return failure(STAGE9_LINK_EFRAME,frame,count);
            base=current;previous_shown=current.displayed;previous_engine=current.engine;
            ready=1U;updates=0U;min_changed=min_visible=count;max_visible=0U;respawn=max_bucket=0U;
            period_max=period_over=period_over3=0U;
            build_ticks=present_ticks=legacy_ticks=0U;
            start_pending=1U;
            /* The first PERIOD interval starts at the last physical display
             * completion. Pending START logging remains inside the criterion. */
            last_completed=completed;window_start=clock_now();continue;
        }
        updates++;
        uint64_t ended=clock_now(),span=ended-window_start;
        if(span>=(uint64_t)BSP_CLINT_HZ*2U){
            stage9_link_snapshot(&current);
            uint32_t missing=current.missing_px-base.missing_px,lines=current.missing_lines-base.missing_lines;
            uint32_t reject=current.rejected-base.rejected,skip=current.skipped-base.skipped;
            uint32_t shown=current.displayed-previous_shown,engine=current.engine-previous_engine;
            fps10=(uint32_t)((uint64_t)updates*BSP_CLINT_HZ*10U/span);
            shown10=(uint32_t)((uint64_t)shown*BSP_CLINT_HZ*10U/span);
            const GardenLogStats *logs=garden_log_stats();
            if(stream_bad(&current,&base))return failure(STAGE9_LINK_EFRAME,frame,count);
            pending=(PendingReport){
                .args={generation,frame,count,speed_q8,min_visible,min_changed,visible,min_visible,max_visible,
                    s9_count,legacy_count,three?legacy_count:legacy_count-(count/PURE_RAY_COUNT)*2U*route_cut(active),
                    built.family_count[0],built.family_count[1],built.family_count[2],built.family_count[3],
                    built.family_count[4],built.family_count[5],built.family_count[6],max_bucket,respawn,
                    current.max_line,updates,(uint32_t)span,(uint32_t)(span>>32),(unsigned)BSP_CLINT_HZ,
                    fps10,shown,engine,checksum,0U,0U,0U,missing,lines,reject,skip,errors,guard,
                    logs->dropped_lines,logs->truncated_lines},
                .max_ticks=period_max,.over60=period_over,.over3=period_over3,
                .build_ticks=build_ticks,.legacy_ticks=legacy_ticks,.present_ticks=present_ticks,
                .sample_updates=updates};
            report_pending=1U;
            period_max=period_over=period_over3=0U;
            previous_shown=current.displayed;previous_engine=current.engine;
            updates=0U;min_changed=min_visible=count;max_visible=0U;respawn=max_bucket=0U;build_ticks=present_ticks=legacy_ticks=0U;window_start=ended;
            hud_dirty[0]=hud_dirty[1]=1U;
        }
    }
}
int pure_bullet_test_run(void){return pure_run(0U);}
int pure_bullet_test_boss_run(void){return pure_run(1U);}
#endif
