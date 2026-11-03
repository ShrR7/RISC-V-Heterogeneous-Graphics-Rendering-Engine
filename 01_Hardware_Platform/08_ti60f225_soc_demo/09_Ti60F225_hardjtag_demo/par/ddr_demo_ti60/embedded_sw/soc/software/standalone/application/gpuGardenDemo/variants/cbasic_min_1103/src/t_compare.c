/* GPL-3.0-or-later. Standalone BASIC comparison with a measured load governor.
 * Right retains original Stage9/legacy bands. Left renders sprites and dirty
 * erase entirely in software into private pages. Copying completed CPU pages
 * and the HUD are common presentation work, not CPU scene acceleration.
 * One shared CPU: FPS is actual distinct presented images, not isolated peaks.
 */
#include "pure_bullet_test.h"
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_log.h"
#include "stage9_link.h"
#include "g9_memory.h"
#include "display_debug.h"
#include "basic_title.h"
#include <string.h>
#define S_MAX 2093U
#define S_KEY 0xF81FU
#define S_NOKEY 0x0004U
#define S_SECONDS 15U
#define S_MIN (PURE_PER_RAY_MIN*PURE_RAY_COUNT)
#define S_STEP PURE_RAY_COUNT
#define S_TARGET_FPS 50U
#define S_TIMEOUT 20000000U
#define S_CPU0 0x00BC0000U
#define S_CPU1 0x00DA0000U
#define S_CPU_STRIDE 4096U
#define S_CPU_BYTES (480U*S_CPU_STRIDE)
#define S_CSTATE 0x00A00000U
#define S_CDESC 0x00A10000U
#define S_RAYS ((const PureBulletRayMeta *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_RAY_OFFSET))
#define S_PIXELS ((const uint16_t *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET))
typedef struct {
    uint32_t before[4];
    PureBulletParticle particles[S_MAX];
    PureBulletPlan plans[S_MAX];
    GpuBlitEntry legacy[27U*PURE_SCATTER_BUCKET] __attribute__((aligned(16)));
    uint32_t after[4];
} SplitState;
typedef struct {
    SplitState model;
    uint64_t dirty[2][30],old_mask[30];
    PureBulletBuildResult built;
} CpuState;
#define GS ((SplitState *)(uintptr_t)G9_ROUTE_ADDR)
#define CS ((CpuState *)(uintptr_t)S_CSTATE)
#define CD ((PureBulletDesc *)(uintptr_t)S_CDESC)
_Static_assert(sizeof(SplitState)<G9_ROUTE_BYTES,"GPU scratch");
_Static_assert(sizeof(CpuState)<S_CDESC-S_CSTATE,"CPU scratch");
_Static_assert(S_CDESC+2048U*16U<G9_OLD_BG_ADDR+G9_OLD_BG_BYTES,"CPU descriptors");
_Static_assert(S_CPU0+S_CPU_BYTES==S_CPU1,"private CPU pages");
_Static_assert(S_CPU1+S_CPU_BYTES+16U<G9_DDR_END,"private CPU DDR bounds");
static unsigned count=S_MAX,cooperate;
static const unsigned mode_alpha=32U,mode_key=S_NOKEY;
static unsigned remaining=S_SECONDS,baked_key_pixels;
static unsigned cpu_front,cpu_back,cpu_stage,cpu_index,cpu_row,cpu_tile,cpu_entry;
static uint32_t cpu_serial,cpu_frame;
static uint64_t cpu_started,cpu_active_ticks,cpu_draw_ticks;
static uint32_t cpu_work_us,cpu_draw_us,cpu_wall_us,cpu_last_checksum;
static unsigned cpu_last_visible,hw_last_visible;
static uint32_t gpu_draw_us,gpu_handoff_us,gpu_engine_us;
static uint32_t gpu_prepare_us,gpu_copy_us,gpu_hud_us,gpu_wall_us;
static unsigned cpu_seq_pending;
static uint32_t cpu_fps10,gpu_fps10,copied_serial[2];
static uint64_t now(void){return clint_getTime(BSP_CLINT);}
static uint32_t usec(uint64_t ticks)
{return (uint32_t)(ticks*1000000U/BSP_CLINT_HZ);}
static uint32_t cpu_page(unsigned bank){return bank?S_CPU1:S_CPU0;}
static void publish(void)
{asm volatile("fence iorw,iorw" ::: "memory");data_cache_invalidate_all();asm volatile("fence iorw,iorw" ::: "memory");}
static char *text_out(char *p,const char *s){while(*s)*p++=*s++;*p=0;return p;}
static char *number_out(char *p,uint32_t n)
{char r[10];unsigned k=0U;do{r[k++]=(char)('0'+n%10U);n/=10U;}while(n);while(k)*p++=r[--k];*p=0;return p;}
static void number_label(int x,int y,const char *label,uint32_t n,unsigned fps)
{
    char s[64],*p=text_out(s,label);
    if(fps){p=number_out(p,n/10U);*p++='.';*p++=(char)('0'+n%10U);*p=0;}
    else number_out(p,n);
    garden_render_text_style(x,y,s,fps?GARDEN_TEXT_LARGE_WHITE:GARDEN_TEXT_SMALL_WHITE);
}
static void gpu_stars(unsigned y,unsigned h)
{
    for(unsigned row=0;row<8U;row++)for(unsigned col=0;col<10U;col++){
        uint32_t seed=(row*10U+col+1U)*2654435761U;
        unsigned sx=col*64U+16U+(seed&31U),sy=120U+row*64U+8U+((seed>>8)&31U);
        unsigned size=(seed&7U)==0U?2U:1U;
        if(sy>=y && sy+size<=y+h && sy+size<=600U)
            garden_render_box(640+(int)sx,(int)sy,(int)size,(int)size,(seed&1U)?0x3186U:0x18C3U);
    }
}
static void headings(void)
{
    garden_render_box(0,0,1280,720,GARDEN_UI_BACKGROUND);
    for(unsigned side=0;side<2U;side++){
        int x=(int)(side*640U);
        garden_render_box(x+8,8,624,100,GARDEN_UI_PANEL);
        garden_render_text_style(x+24,56,side?"GRAPHICS ACCELERATOR":"CPU",GARDEN_TEXT_LARGE_WHITE);
        number_label(x+24,88,"BULLETS: ",count,0U);
        garden_render_text(x+248,88,"ALPHA OFF / COLOR KEY OFF");
        garden_render_box(x,118,640,2,GARDEN_UI_CYAN);
        garden_render_box(x,600,640,2,GARDEN_UI_CYAN);
        garden_render_box(x+8,612,624,84,GARDEN_UI_PANEL);
    }
    garden_render_box(638,44,4,74,GARDEN_UI_CYAN);
    garden_render_box(638,602,4,118,GARDEN_UI_CYAN);
    garden_render_box(0,120,1280,480,0U);gpu_stars(120,480);
}
static void hud(void)
{
    unsigned saved=(unsigned)garden_render_is_cpu();garden_render_mode(1);
    for(unsigned side=0;side<2U;side++)
        garden_render_box((int)(side*640U)+24,636,520,24,GARDEN_UI_PANEL);
    number_label(24,636,"FPS: ",cpu_fps10,1U);
    number_label(664,636,"FPS: ",gpu_fps10,1U);
    garden_render_mode((int)saved);
}
static unsigned guards(void)
{
    unsigned bad=0U;
    for(unsigned w=0;w<4U;w++){
        bad+=GS->before[w]!=(0x53503010U+w);bad+=GS->after[w]!=(0x53503020U+w);
        bad+=CS->model.before[w]!=(0x53503110U+w);bad+=CS->model.after[w]!=(0x53503120U+w);
        for(unsigned b=0;b<2U;b++)bad+=*(volatile uint32_t *)(uintptr_t)(cpu_page(b)+479U*S_CPU_STRIDE+1280U+w*4U)!=(0x53503210U+b*16U+w);
    }
    return bad;
}
static void mark_cpu_dirty(unsigned x,unsigned y)
{
    unsigned a=x/16U,b=(x+15U)/16U;
    uint64_t m=(UINT64_C(1)<<a)|(UINT64_C(1)<<b);
    CS->dirty[cpu_back][y/16U]|=m;CS->dirty[cpu_back][(y+15U)/16U]|=m;
}
static void cpu_model_one(unsigned i)
{
    unsigned ray=CS->model.plans[i].ray,age=CS->model.particles[i].age+1U;
    if(age>=CS->model.particles[i].ttl){age=0U;CS->built.respawn++;}
    CS->model.particles[i].age=(uint16_t)age;
    const PureBulletRayMeta *r=S_RAYS+ray;unsigned p=r->packed;
    int px=((int)(int16_t)r->velocity*(int)(age*PURE_SPEED_DEFAULT_Q8)+(1<<21))>>22;
    int py=((int)(int16_t)(r->velocity>>16)*(int)(age*PURE_SPEED_DEFAULT_Q8)+(1<<21))>>22;
    unsigned x=(unsigned)(320+px-(int)(p&255U)),y=(unsigned)(16+py-(int)((p>>8)&255U));
    unsigned tile=(p>>16)&255U,cut=count>=2093U?6U:7U;
    uint32_t *crc=&CS->built.checksum;
    *crc=((*crc<<5)|(*crc>>27))^((x+320U)|((y+120U)<<16));
    if(ray>=cut && ray<91U-cut && y+136U<=480U){
        unsigned n=CS->built.s9_count++;
        CD[n]=(PureBulletDesc){(int16_t)x,(int16_t)y,0,0,(uint16_t)tile,1U,0U};
    }else{
        unsigned n=tile*PURE_SCATTER_BUCKET+CS->built.legacy_count[tile]++;
        CS->model.legacy[n].dst=cpu_page(cpu_back)+y*S_CPU_STRIDE+x*2U;
    }
    CS->built.visible++;mark_cpu_dirty(x,y);
}
__attribute__((optimize("O3"))) static void cpu_sprite(uint32_t address,unsigned tile)
{
    const uint16_t *src=(const uint16_t *)(uintptr_t)(G9_ATLAS0_ADDR+tile*512U);
    for(unsigned row=0;row<16U;row++){
        volatile uint16_t *dst=(volatile uint16_t *)(uintptr_t)(address+row*S_CPU_STRIDE);
        for(unsigned col=0;col<16U;col++){
            dst[col]=src[row*16U+col];
        }
    }
}
/* One bounded unit: no GPU, HUD or logging work in the CPU scene renderer. */
static void cpu_one(void)
{
    uint64_t entered=now();
    if(cpu_stage==0U){
        cpu_started=entered;cpu_active_ticks=cpu_draw_ticks=0U;cpu_index=0U;
        memcpy(CS->old_mask,CS->dirty[cpu_back],sizeof(CS->old_mask));
        memset(CS->dirty[cpu_back],0,sizeof(CS->dirty[cpu_back]));
        CS->built=(PureBulletBuildResult){.checksum=0x2000C0DEU};cpu_stage=1U;
    }else if(cpu_stage==1U){
        cpu_model_one(cpu_index++);
        if(cpu_index==count){cpu_row=0U;cpu_stage=2U;}
    }else if(cpu_stage==2U){
        uint64_t mask=CS->old_mask[cpu_row/16U];
        volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(cpu_page(cpu_back)+cpu_row*S_CPU_STRIDE);
        for(unsigned c=0;c<40U;c++)if(mask&(UINT64_C(1)<<c))for(unsigned k=0;k<8U;k++)p[c*8U+k]=0U;
        if(++cpu_row==480U){cpu_index=0U;cpu_stage=3U;}
    }else if(cpu_stage==3U){
        unsigned row=cpu_index/10U,col=cpu_index%10U;
        uint32_t seed=(cpu_index+1U)*2654435761U;
        unsigned x=col*64U+16U+(seed&31U),y=row*64U+8U+((seed>>8)&31U),size=(seed&7U)==0U?2U:1U;
        if(y+size<=480U)for(unsigned a=0;a<size;a++)for(unsigned b=0;b<size;b++)
            *(volatile uint16_t *)(uintptr_t)(cpu_page(cpu_back)+(y+a)*S_CPU_STRIDE+(x+b)*2U)=(seed&1U)?0x3186U:0x18C3U;
        if(++cpu_index==80U){cpu_tile=cpu_entry=0U;cpu_stage=4U;}
    }else if(cpu_stage==4U){
        while(cpu_tile<27U && cpu_entry>=CS->built.legacy_count[cpu_tile]){cpu_tile++;cpu_entry=0U;}
        if(cpu_tile<27U){uint64_t began=now();cpu_sprite(CS->model.legacy[cpu_tile*PURE_SCATTER_BUCKET+cpu_entry++].dst,cpu_tile);cpu_draw_ticks+=now()-began;}
        else{cpu_index=0U;cpu_stage=5U;}
    }else if(cpu_stage==5U){
        if(cpu_index<CS->built.s9_count){const PureBulletDesc *d=CD+cpu_index++;
            uint64_t began=now();cpu_sprite(cpu_page(cpu_back)+(unsigned)d->y*S_CPU_STRIDE+(unsigned)d->x*2U,d->sprite);cpu_draw_ticks+=now()-began;
        }else cpu_stage=6U;
    }else{
        publish();cpu_front=cpu_back;cpu_back^=1U;cpu_serial++;cpu_frame++;
        cpu_draw_us=usec(cpu_draw_ticks);cpu_work_us=usec(cpu_active_ticks+now()-entered);
        cpu_wall_us=usec(now()-cpu_started);cpu_last_checksum=CS->built.checksum;
        cpu_last_visible=CS->built.visible;
        if(cpu_frame<=2U || cpu_frame==120U)cpu_seq_pending=cpu_frame;
        cpu_stage=0U;
    }
    cpu_active_ticks+=now()-entered;
}
void split_t_idle_work(void){if(cooperate)cpu_one();}
/* The released copy engine has a two-millisecond command watchdog. Split a
 * completed software surface into short row commands; no scene is redrawn. */
static int copy_rows(uint32_t src,uint32_t fb,unsigned y,unsigned rows)
{
    unsigned end=y+rows;
    while(y<end){
        unsigned h=end-y;if(h>64U)h=64U;
        int rc=gpu_copy(src+y*S_CPU_STRIDE,fb+(y+120U)*2560U,640U,h,S_CPU_STRIDE,2560U);
        if(rc)return rc;
        y+=h;
    }
    return GPU_OK;
}
static int setup(void)
{
    if(count<S_MIN || count>S_MAX || count%S_STEP)return GPU_EINVAL;
    baked_key_pixels=0U;
    stage9_link_alpha(mode_alpha);stage9_link_key((uint16_t)mode_key);
    for(unsigned i=0;i<4U;i++){
        GS->before[i]=0x53503010U+i;GS->after[i]=0x53503020U+i;
        CS->model.before[i]=0x53503110U+i;CS->model.after[i]=0x53503120U+i;
    }
    unsigned n=0U;
    for(unsigned wave=0;wave<count/91U;wave++)for(unsigned ray=0;ray<91U;ray++){
        GS->plans[n]=CS->model.plans[n]=(PureBulletPlan){(uint16_t)ray,(uint16_t)(wave+1U)};n++;
    }
    pure_bullet_test_model_init(GS->particles,count,GS->plans,S_RAYS,PURE_SPEED_DEFAULT_Q8);
    pure_bullet_test_model_init(CS->model.particles,count,CS->model.plans,S_RAYS,PURE_SPEED_DEFAULT_Q8);
    for(unsigned b=0;b<2U;b++){
        volatile uint16_t *dst=(volatile uint16_t *)(uintptr_t)(b?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR);
        for(unsigned i=0;i<G9_ATLAS_BYTES/2U;i++){
            uint16_t v=i<27U*256U?S_PIXELS[i]:(uint16_t)mode_key;
            if(i<27U*256U){
                if(v==S_NOKEY)return GPU_EINVAL;
                if(v==S_KEY){v=0U;if(!b)baked_key_pixels++;}
            }
            dst[i]=v;
        }
        for(unsigned y=0;y<480U;y++){
            volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(cpu_page(b)+y*S_CPU_STRIDE);
            for(unsigned x=0;x<320U;x++)p[x]=0U;
        }
        for(unsigned w=0;w<4U;w++)*(volatile uint32_t *)(uintptr_t)(cpu_page(b)+479U*S_CPU_STRIDE+1280U+w*4U)=0x53503210U+b*16U+w;
    }
    memset(CS->dirty,0,sizeof(CS->dirty));cpu_front=1U;cpu_back=0U;
    cpu_stage=cpu_serial=cpu_frame=cpu_seq_pending=0U;cpu_fps10=gpu_fps10=0U;
    cpu_last_visible=hw_last_visible=0U;
    cpu_work_us=cpu_draw_us=cpu_wall_us=gpu_draw_us=gpu_handoff_us=gpu_engine_us=0U;
    gpu_prepare_us=gpu_copy_us=gpu_hud_us=gpu_wall_us=0U;
    copied_serial[0]=copied_serial[1]=UINT32_MAX;
    publish();
    do{cpu_one();}while(!cpu_serial);
    garden_render_mode(0);garden_render_batch_mode(0);
    for(unsigned b=0;b<2U;b++){
        render_begin();headings();hud();
        int title_rc=basic_title_draw(576,4);if(title_rc)return title_rc;
        int rc=copy_rows(cpu_page(cpu_front),display_back_addr(),0U,480U);if(rc)return rc;
        copied_serial[display_back_sel()]=cpu_serial;rc=render_end(S_TIMEOUT);if(rc)return rc;
    }
    return guards()?GPU_EOVERLAP:GPU_OK;
}
static int bottom_begin(uint32_t fb)
{
    int rc=gpu_wait_idle(3000000U);if(rc)return rc;
    GPU_DST_ADDR=fb+464U*2560U+1280U;GPU_WIDTH=640U;GPU_HEIGHT=136U;
    GPU_DST_STRIDE=2560U;GPU_COLOR=0U;asm volatile("fence iorw,iorw" ::: "memory");
    GPU_CTRL=0U;GPU_CTRL=GPU_CTRL_FILL;return GPU_OK;
}
static int gpu_legacy(uint32_t fb,const PureBulletBuildResult *b)
{
    unsigned rows=count>=2093U?PURE_FAST_LEGACY_ROWS:PURE_UNIFORM_LEGACY_ROWS;
    int rc=gpu_fill(fb+120U*2560U+1280U,640U,rows,2560U,0U);if(rc)return rc;
    gpu_stars(120U,rows);gpu_stars(464U,136U);
    for(unsigned tile=0;tile<27U;tile++)if(b->legacy_count[tile]){
        rc=gpu_bitblt_batch(GS->legacy+tile*PURE_SCATTER_BUCKET,b->legacy_count[tile],G9_ATLAS0_ADDR+tile*512U,16U,16U,32U,2560U,mode_key,mode_alpha);if(rc)return rc;
    }
    return gpu_wait_idle(3000000U);
}
/* Geometry and runtime atlas are immutable for one count epoch. The only
 * allowed count transition follows a completed frame and a drained Stage9. */
typedef struct {
    DisplayDebugTotals display;
    unsigned front,drop,trunc;
} BasicBaseline;
static void basic_record(const char *tag,unsigned epoch,unsigned assess,
                         unsigned frame,uint64_t span,unsigned hn,unsigned cn,
                         const Stage9LinkStats *st,const BasicBaseline *first,
                         unsigned pass,const char *action,unsigned next_count)
{
    const DisplayDebugTotals *d=display_debug_totals();
    const GardenLogStats *logs=garden_log_stats();
    garden_log_printf("[%s] epoch=%u assess=%u frame=%u count=%u per_side=%u total=%u ticks=%u hz=%u cpu_presented=%u hw_presented=%u cpu_fps1000=%u hw_fps1000=%u cpu_visible=%u hw_visible=%u cpu_draw_us=%u gpu_blit_us=%u copy_us=%u hw_submissions=%u cpu_completed=%u alpha=%u key=0x%x alpha_enabled=0 key_matches=0 copy_all=1 missing_px=%u missing_lines=%u reject=%u skip=%u errors=%u guard=%u front_reject=%u log_dropped=%u log_truncated=%u disp_px=%u disp_line=%u disp_full=%u pass=%u action=%s next_count=%u\r\n",
        tag,epoch,assess,frame,count,count,count*2U,(uint32_t)span,BSP_CLINT_HZ,cn,hn,
        span?(unsigned)((uint64_t)cn*BSP_CLINT_HZ*1000U/span):0U,
        span?(unsigned)((uint64_t)hn*BSP_CLINT_HZ*1000U/span):0U,
        cpu_last_visible,hw_last_visible,cpu_draw_us,gpu_draw_us,gpu_copy_us,
        st->submissions,cpu_serial,mode_alpha,mode_key,
        st->missing_px-st->initial_missing_pixels,st->missing_lines-st->initial_missing_lines,
        st->rejected-st->initial_rejected_writes,st->skipped-st->initial_skipped_frames,st->errors,
        guards()+g9_memory_guard_check(),display_stats()->front_rejects-first->front,
        logs->dropped_lines-first->drop,logs->truncated_lines-first->trunc,
        (unsigned)(d->px-first->display.px),(unsigned)(d->lines-first->display.lines),
        (unsigned)(d->full-first->display.full),pass,action,next_count);
}
int t_compare_run(void)
{
    unsigned epoch=0U,assess=0U;
    BasicBaseline first={0};
    int rc=stage9_link_init();if(rc)return rc;
    cooperate=0U;count=S_MAX;
    for(;;){
        unsigned frame=0U,bank=0U,warm=0U,dirty=0U,wn=0U,wc=0U,last_cpu=0U;
        unsigned an=0U,ac=0U,next_count=count;
        uint64_t window=0U,last_done=0U,assess_begin=0U,preparing=now(),prepared=0U;
        Stage9LinkStats st={0};
        display_debug_sample();first.display=*display_debug_totals();
        first.front=display_stats()->front_rejects;
        first.drop=garden_log_stats()->dropped_lines;first.trunc=garden_log_stats()->truncated_lines;
        remaining=S_SECONDS;assess=0U;
        rc=setup();if(rc)goto failed;
        prepared=now()-preparing;
        display_debug_sample();
        garden_log_printf("[BASIC_CONFIG] epoch=%u per_side=%u total=%u tile=16x16 viewport_each=640x480 alpha=32 alpha_enabled=0 key=0x4 key_matches=0 copy_all=1 bake_key_to_black=%u source_pixels=6912 rays=91 speed_q8=612 assess_seconds=15 target_hw_fps=50 min_count=%u step=91 setup_ticks=%u hz=%u shared_cpu=1 isolated_peak=0 order=legacy_then_s9 trajectory=per_update copy=common_completed_surface health_includes_prep=1\r\n",
            epoch,count,count*2U,baked_key_pixels,S_MIN,(uint32_t)prepared,BSP_CLINT_HZ);
        for(;;){
            /* This independent demonstration has no game/menu exit shortcut. */
            while(uart_readOccupancy(BSP_UART_TERMINAL))(void)uart_read(BSP_UART_TERMINAL);
            uint64_t frame_began=now();
            render_begin();unsigned sel=display_back_sel();uint32_t fb=display_back_addr();
            if(display_check_render_target(fb)!=GPU_OK){rc=GPU_ESTATE;goto failed;}
            rc=bottom_begin(fb);if(rc)goto failed;
            bank^=1U;uint32_t da=bank?G9_DESC1_ADDR:G9_DESC0_ADDR;
            PureBulletDesc *desc=(PureBulletDesc *)(uintptr_t)da;PureBulletBuildResult built;
            rc=pure_bullet_test_model_build(GS->particles,desc,&GS->legacy[0].dst,count,fb+640U,GS->plans,S_RAYS,PURE_SPEED_DEFAULT_Q8,&built);
            if(rc || built.visible!=count){rc=GPU_ESTATE;goto failed;}
            hw_last_visible=built.visible;
            for(unsigned i=0;i<built.s9_count;i++)desc[i].x+=320;
            rc=gpu_wait_idle(3000000U);if(rc)goto failed;publish();
            unsigned copying=copied_serial[sel]!=cpu_serial;
            uint32_t copy_serial=cpu_serial,copy_source=cpu_page(cpu_front);
            uint64_t copy_began=now();
            if(copying){
                rc=copy_rows(copy_source,fb,0U,480U);if(rc)goto failed;
                copied_serial[sel]=copy_serial;
            }
            gpu_copy_us=usec(now()-copy_began);
            uint64_t draw_began=now();rc=gpu_legacy(fb,&built);if(rc)goto failed;
            gpu_draw_us=usec(now()-draw_began);publish();
            /* Common HUD uses CPU stores; it cannot enable an Alpha GPU op. */
            uint64_t hud_began=now();
            if(dirty&(1U<<sel)){hud();dirty&=~(1U<<sel);}
            gpu_hud_us=usec(now()-hud_began);publish();
            gpu_prepare_us=usec(now()-frame_began);
            cooperate=warm==2U && !copying;
            uint64_t handoff_began=now();
            rc=stage9_link_present(bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR,da,built.s9_count,fb,sel);
            gpu_handoff_us=usec(now()-handoff_began);cooperate=0U;if(rc)goto failed;
            if(*(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x120U)!=S_NOKEY ||
               *(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x124U)!=32U){rc=GPU_ESTATE;goto failed;}
            /* No framebuffer writes below the swap request in present(). */
            if(cpu_seq_pending){
                garden_log_printf("[BASIC_SEQUENCE] epoch=%u count=%u backend=CPU frame=%u checksum=0x%x visible=%u\r\n",epoch,count,cpu_seq_pending,cpu_last_checksum,cpu_last_visible);cpu_seq_pending=0U;
            }
            garden_log_pump();
            if(warm==2U){
                uint64_t deadline=last_done+BSP_CLINT_HZ/60U-90000U;
                while(now()<deadline)cpu_one();
            }
            rc=render_end(S_TIMEOUT);if(rc)goto failed;
            uint64_t done=now();gpu_wall_us=usec(done-frame_began);frame++;display_debug_sample();
            if(frame<=2U || frame==120U)garden_log_printf("[BASIC_SEQUENCE] epoch=%u count=%u backend=HW frame=%u checksum=0x%x visible=%u\r\n",epoch,count,frame,built.checksum,built.visible);
            if(g9_memory_guard_check() || guards() || display_stats()->front_rejects!=first.front ||
               cpu_last_visible!=count || hw_last_visible!=count){rc=GPU_EOVERLAP;goto failed;}
            if(warm<2U){
                if(++warm==2U){
                    window=assess_begin=done;wn=wc=an=ac=0U;last_cpu=copied_serial[sel];dirty=3U;
                    garden_log_printf("[BASIC_START] epoch=%u assess=0 count=%u prep_ticks=%u formal_start_hi=%u formal_start_lo=%u hz=%u warm_hw_frames=2 prepared_cpu_frames=1 assessment_seconds=15\r\n",epoch,count,(uint32_t)(done-preparing),(uint32_t)(done>>32),(uint32_t)done,BSP_CLINT_HZ);
                }
            }else{
                wn++;an++;
                if(copied_serial[sel]!=last_cpu){wc++;ac++;last_cpu=copied_serial[sel];}
                uint64_t actual=done-assess_begin;
                unsigned elapsed=(unsigned)(actual/BSP_CLINT_HZ);
                unsigned left=elapsed<S_SECONDS?S_SECONDS-elapsed:0U;
                if(left!=remaining){remaining=left;dirty=3U;}
                unsigned ready=actual>=(uint64_t)BSP_CLINT_HZ*S_SECONDS;
                if(done-window>=(uint64_t)BSP_CLINT_HZ || ready){
                    uint64_t span=done-window;
                    cpu_fps10=(uint32_t)((uint64_t)wc*BSP_CLINT_HZ*10U/span);
                    gpu_fps10=(uint32_t)((uint64_t)wn*BSP_CLINT_HZ*10U/span);
                    stage9_link_snapshot(&st);gpu_engine_us=st.last_frame_cycles/100U;
                    basic_record("BASIC",epoch,assess,frame,span,wn,wc,&st,&first,2U,"WINDOW",count);
                    dirty=3U;wn=wc=0U;window=done;
                }
                if(ready){
                    /* Compare integer products, not rounded UI tenths. */
                    unsigned pass=(uint64_t)an*BSP_CLINT_HZ>=(uint64_t)S_TARGET_FPS*actual;
                    const char *action=pass?"KEEP":count>S_MIN?"REDUCE":"FLOOR";
                    next_count=(!pass && count>S_MIN)?count-S_STEP:count;
                    if(next_count<S_MIN)next_count=S_MIN;
                    stage9_link_snapshot(&st);
                    basic_record("BASIC_ASSESS",epoch,assess,frame,actual,an,ac,&st,&first,pass,action,next_count);
                    if(next_count!=count){last_done=done;break;}
                    /* KEEP/FLOOR do not reset either model or CPU work. */
                    assess++;assess_begin=window=done;wn=wc=an=ac=0U;
                    remaining=S_SECONDS;dirty=3U;
                }
            }
            last_done=done;
        }
        /* All old-count data remain untouched until this drain succeeds. */
        cooperate=0U;rc=stage9_link_stop();if(rc)goto failed;
        garden_log_printf("[BASIC_COUNT_CHANGE] epoch=%u assess=%u old_count=%u new_count=%u step=91 drained=1 cpu_task_discarded=1 next_setup=full\r\n",epoch,assess,count,next_count);
        count=next_count;epoch++;
        rc=stage9_link_init();if(rc)goto failed;
    }
failed:
    cooperate=0U;
    garden_log_printf("[BASIC_FATAL] rc=%d epoch=%u assess=%u count=%u guard=%u front_reject=%u log_dropped=%u log_truncated=%u\r\n",
        rc,epoch,assess,count,guards()+g9_memory_guard_check(),display_stats()->front_rejects-first.front,
        garden_log_stats()->dropped_lines-first.drop,garden_log_stats()->truncated_lines-first.trunc);
    display_debug_report("BASIC_FATAL",0U);garden_log_pump();(void)stage9_link_stop();return rc;
}
