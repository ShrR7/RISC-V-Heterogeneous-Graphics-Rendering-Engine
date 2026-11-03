/* GPL-3.0-or-later. Independent CPU/hardware with matched source and effects.
 * Hardware: original asynchronous bottom erase, upper legacy band, Stage9.
 * CPU: software dirty-cell union covers ALL persistent rasterized sprites.
 * Both restore the same stars and keep legacy-then-Stage9 painter order.
 * Every backend receives the original unprocessed source pixels and uses
 * the same Alpha/key rules and legacy-then-Stage9 painter order.
 */
#include "pure_bullet_test.h"
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "gpu_alpha_ref.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_log.h"
#include "stage9_link.h"
#include "g9_memory.h"
#include "display_debug.h"
#include <string.h>
#define B_MAX 2730U
#define B_TIMEOUT 20000000U
#define B_NOKEY 4U
#define B_COMMON_CRC 0xFD0ECEEBU
#define B_ATLAS_CRC 0x7D029D94U
#define B_PIXELS ((const uint16_t *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_PIXEL_OFFSET))
#define B_RAYS ((const PureBulletRayMeta *)(uintptr_t)(PURE_SCATTER_BASE+PURE_SCATTER_RAY_OFFSET))
typedef struct {
    uint32_t before[4];PureBulletParticle p[B_MAX];PureBulletPlan plans[B_MAX];
    GpuBlitEntry legacy[27U*PURE_SCATTER_BUCKET] __attribute__((aligned(16)));
    uint32_t after[4];
} BStore;
typedef struct {uint32_t before[4];uint64_t masks[2][30];uint32_t after[4];} BHistory;
#define BS ((BStore *)(uintptr_t)G9_ROUTE_ADDR)
#define BH ((BHistory *)(uintptr_t)0x00BC0000U)
_Static_assert(sizeof(BStore)<G9_ROUTE_BYTES,"benchmark scratch");
static unsigned mode_alpha,mode_key;
static uint32_t runtime_source_crc,runtime_atlas_crc[2];
static uint64_t tick(void){return clint_getTime(BSP_CLINT);}
static uint32_t microseconds(uint64_t ticks)
{return (uint32_t)(ticks*1000000U/BSP_CLINT_HZ);}
static unsigned run_serial;
typedef struct {unsigned live1000,mean1000;} LiveHud;
static unsigned hud_painted[2][2],hud_valid[2];
static char *decimal(char *p,uint32_t n)
{
    char reversed[10];unsigned k=0U;
    do{reversed[k++]=(char)('0'+n%10U);n/=10U;}while(n);
    while(k){*p++=reversed[--k];}
    *p=0;return p;
}
static void publish(void)
{asm volatile("fence iorw,iorw" ::: "memory");data_cache_invalidate_all();asm volatile("fence iorw,iorw" ::: "memory");}
static uint32_t bytes_crc(const volatile uint8_t *p,unsigned n)
{
    uint32_t c=0xFFFFFFFFU;
    for(unsigned i=0;i<n;i++){
        c^=p[i];
        for(unsigned bit=0;bit<8U;bit++)c=(c>>1)^((0U-(c&1U))&0xEDB88320U);
    }
    return c^0xFFFFFFFFU;
}
static void atlas_record(unsigned cycle,unsigned case_id,unsigned backend)
{
    char list[27U*9U],*p=list;
    static const char hex[]="0123456789abcdef";
    for(unsigned tile=0;tile<27U;tile++){
        uint32_t c=bytes_crc((const volatile uint8_t *)(uintptr_t)(G9_ATLAS0_ADDR+tile*512U),512U);
        for(unsigned digit=0;digit<8U;digit++)*p++=hex[(c>>(28U-digit*4U))&15U];
        if(tile<26U)*p++=',';
    }
    *p=0;
    garden_log_printf("[SIXL_ATLAS] cycle=%u case=%u backend=%u common_source_crc=0x%x atlas0_crc=0x%x atlas1_crc=0x%x tiles=27 tile_crcs=%s\r\n",
        cycle,case_id,backend,runtime_source_crc,runtime_atlas_crc[0],runtime_atlas_crc[1],list);
}
static unsigned guards(void)
{
    unsigned bad=0;
    for(unsigned i=0;i<4;i++){
        bad+=BS->before[i]!=(0x4D4F1010U+i);bad+=BS->after[i]!=(0x4D4F1020U+i);
        bad+=BH->before[i]!=(0x4D4F1030U+i);bad+=BH->after[i]!=(0x4D4F1040U+i);
    }
    return bad;
}
/* This exact software raster is used by both the probe and the timed CPU
 * scene. An absent key disables matches, while the conditional still exists. */
__attribute__((optimize("O3")))
static void cpu_raster(uint32_t source,uint32_t destination,unsigned width,
                       unsigned height,unsigned source_stride,unsigned destination_stride)
{
    for(unsigned y=0;y<height;y++){
        const volatile uint16_t *in=(const volatile uint16_t *)(uintptr_t)(source+y*source_stride);
        volatile uint16_t *out=(volatile uint16_t *)(uintptr_t)(destination+y*destination_stride);
        for(unsigned x=0;x<width;x++){
            uint16_t value=in[x];
            if(value!=mode_key)out[x]=mode_alpha==32U?value:gpu_alpha_rgb565(value,out[x],mode_alpha);
        }
    }
}
int modes_probe(void)
{
    unsigned old_key_pixels=0U,original_black=0U,no_key_matches=0U;
    for(unsigned i=0;i<27U*256U;i++){
        old_key_pixels+=B_PIXELS[i]==0xF81FU;original_black+=B_PIXELS[i]==0U;
        no_key_matches+=B_PIXELS[i]==B_NOKEY;
    }
    if(old_key_pixels!=5142U || original_black!=4U || no_key_matches)return GPU_EINVAL;
    unsigned errors=0U;const uint32_t src=ASSET_BASE_ADDR+ASSET_SIZE_BYTES-1024U,dst=src+128U;
    for(unsigned mode=0;mode<3U;mode++){
        unsigned alpha=mode==0U?32U:16U,key=mode==1U?B_NOKEY:0xF81FU;
        for(unsigned i=0;i<32U;i++){
            uint16_t value=i%3U==0U?0xF81FU:i%7U==0U?0U:(uint16_t)(0x181FU+i*997U);
            *(volatile uint16_t *)(uintptr_t)(src+i*2U)=value;
            *(volatile uint16_t *)(uintptr_t)(dst+i*2U)=0x39E7U;
        }
        publish();int rc=gpu_alpha_bitblt(src,dst,8,4,16,16,key,alpha);if(rc)return rc;publish();
        for(unsigned i=0;i<32U;i++){
            uint16_t v=*(const volatile uint16_t *)(uintptr_t)(src+i*2U);
            uint16_t expected=v==key?0x39E7U:gpu_alpha_rgb565(v,0x39E7U,alpha);
            errors+=*(const volatile uint16_t *)(uintptr_t)(dst+i*2U)!=expected;
        }
    }
    garden_log_printf("[SIXL_PIXEL_PROBE] errors=%u modes=3 pixels=96 original_key_pixels=%u original_black_pixels=%u source_pixels=6912 key=0xf81f key_matches=%u no_key=0x%x no_key_matches=%u alpha=rounded_rgb565\r\n",errors,old_key_pixels,original_black,old_key_pixels,B_NOKEY,no_key_matches);
    if(errors)return GPU_EHW;
    GPU_CTRL=0U;GPU_ALPHA=32U;
    for(unsigned mode=0;mode<3U;mode++){
        mode_alpha=mode==0U?32U:16U;mode_key=mode==1U?B_NOKEY:0xF81FU;
        for(unsigned i=0;i<32U;i++){
            uint16_t v=i%3U==0U?0xF81FU:i%7U==0U?0U:(uint16_t)(0x181FU+i*997U);
            *(volatile uint16_t *)(uintptr_t)(src+i*2U)=v;
            *(volatile uint16_t *)(uintptr_t)(dst+i*2U)=0x39E7U;
        }
        cpu_raster(src,dst,8U,4U,16U,16U);
        for(unsigned i=0;i<32U;i++){
            uint16_t v=*(const volatile uint16_t *)(uintptr_t)(src+i*2U);
            uint16_t expected=v==mode_key?0x39E7U:gpu_alpha_rgb565(v,0x39E7U,mode_alpha);
            errors+=*(const volatile uint16_t *)(uintptr_t)(dst+i*2U)!=expected;
        }
    }
    garden_log_printf("[SIXL_CPU_PROBE] errors=%u modes=3 pixels=96 includes_black=1 includes_f81f=1 comparator_sw=1 keyskip_tested=1 alpha16_tested=1 actual_raster=1\r\n",errors);
    return errors?GPU_EHW:GPU_OK;
}
/* Existing 8x8 glyphs are scaled by software, with no extra texture assets. */
static int centered_scaled_text(int center,int y,const char *text,unsigned scale,
                                uint16_t color)
{
    unsigned len=(unsigned)strlen(text),advance=8U*scale;
    int x=center-(int)(len*advance/2U);
    if(!scale || scale>4U || x<0 || x+(int)(len*advance)>1280 || y<0 || y+(int)advance>720)return GPU_EINVAL;
    uint32_t fb=display_back_addr();
    if(display_check_render_target(fb)!=GPU_OK)return GPU_ESTATE;
    for(unsigned i=0U;i<len;i++){
        if(text[i]==' ')continue;
        const garden_sprite_t *glyph=garden_glyph_asset(text[i],0U);
        if(!glyph || glyph->width!=8U || glyph->height!=8U ||
           glyph->offset+128U>GARDEN_ATLAS_BYTES)return GPU_EINVAL;
        const uint16_t *src=(const uint16_t *)(uintptr_t)(GARDEN_ATLAS_ADDRESS+glyph->offset);
        for(unsigned row=0U;row<8U;row++)for(unsigned col=0U;col<8U;col++){
            if(src[row*8U+col]==GARDEN_KEY_COLOR)continue;
            for(unsigned yy=0U;yy<scale;yy++){
                volatile uint16_t *dst=(volatile uint16_t *)(uintptr_t)(fb+(unsigned)(y+(int)(row*scale+yy))*DISPLAY_STRIDE_BYTES+(unsigned)(x+(int)(i*advance+col*scale))*2U);
                for(unsigned xx=0U;xx<scale;xx++)dst[xx]=color;
            }
        }
    }
    return GPU_OK;
}
static int live_hud(unsigned backend,const LiveHud *hud,unsigned dirty,unsigned limit)
{
    unsigned sel=display_back_sel(),painted=0U;
    for(unsigned field=0U;field<2U;field++){
        unsigned bit=1U<<field;if(!(dirty&bit))continue;
        unsigned v=field?hud->mean1000:hud->live1000;
        if((hud_valid[sel]&bit) && hud_painted[sel][field]==v){dirty&=~bit;continue;}
        if(painted==limit)break;
        if(!painted)garden_render_mode(1);
        char text[24],*p=decimal(text,v/1000U);*p++='.';
        *p++=(char)('0'+v/100U%10U);*p++=(char)('0'+v/10U%10U);*p++=(char)('0'+v%10U);*p=0;
        int y=field?392:256;
        garden_render_box(104,y,112,20,GARDEN_UI_PANEL);
        int rc=centered_scaled_text(160,y,text,2U,GARDEN_UI_CYAN);if(rc)return rc;
        hud_painted[sel][field]=v;hud_valid[sel]|=bit;dirty&=~bit;painted++;
    }
    if(painted)garden_render_mode((int)backend);
    return (int)dirty;
}
static int header(unsigned backend,unsigned mode,unsigned count,unsigned case_id,
                  const LiveHud *hud)
{
    char info[64],*p=info;const char *prefix="CASE ";while(*prefix){*p++=*prefix++;}
    p=decimal(p,case_id);prefix=" / 6    SPRITES ";while(*prefix){*p++=*prefix++;}
    p=decimal(p,count);prefix="    AUTO 10 SEC";while(*prefix){*p++=*prefix++;}*p=0;
    unsigned alpha_on=mode!=0U,key_on=mode!=1U;
    hud_valid[0]=hud_valid[1]=0U;
    garden_render_batch_mode(0);garden_render_mode(1);
    for(unsigned i=0U;i<2U;i++){
        render_begin();garden_render_mode(1);
        garden_render_box(0,0,1280,720,GARDEN_UI_BACKGROUND);
        garden_render_box(24,20,1232,76,GARDEN_UI_PANEL);
        const char *title=backend?"CPU":"GRAPHICS ACCELERATOR";
        int tx=640-(int)strlen(title)*8;
        garden_render_text_style(tx,48,title,GARDEN_TEXT_LARGE_WHITE);
        garden_render_text(40,88,info);
        garden_render_box(24,120,272,480,GARDEN_UI_PANEL);
        garden_render_box(984,120,272,480,GARDEN_UI_PANEL);
        garden_render_box(318,118,644,2,GARDEN_UI_CYAN);garden_render_box(318,600,644,2,GARDEN_UI_CYAN);
        garden_render_box(318,120,2,480,GARDEN_UI_CYAN);garden_render_box(960,120,2,480,GARDEN_UI_CYAN);
        garden_render_box(320,120,640,480,0U);garden_ui_stars(320,120,640,480);
        garden_render_text_style(96,200,"LIVE FPS",GARDEN_TEXT_LARGE_WHITE);
        garden_render_text_style(96,336,"MEAN FPS",GARDEN_TEXT_LARGE_WHITE);
        garden_render_text_style(1080,180,"ALPHA",GARDEN_TEXT_LARGE_WHITE);
        int rc=centered_scaled_text(1120,224,alpha_on?"ON":"OFF",4U,alpha_on?GARDEN_UI_CYAN:0xAD55U);if(rc)return rc;
        garden_render_text_style(1024,372,"COLOR KEYING",GARDEN_TEXT_LARGE_WHITE);
        rc=centered_scaled_text(1120,416,key_on?"ON":"OFF",4U,key_on?GARDEN_UI_CYAN:0xAD55U);if(rc)return rc;
        rc=live_hud(backend,hud,3U,2U);if(rc<0)return rc;
        publish();rc=render_end(B_TIMEOUT);if(rc)return rc;
    }
    garden_render_mode((int)backend);return GPU_OK;
}
__attribute__((optimize("O3"))) static void cpu_zero(uint32_t dst,unsigned width,unsigned height)
{
    for(unsigned y=0;y<height;y++){
        volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(dst+y*2560U);
        for(unsigned x=0;x<width/2U;x++)p[x]=0;
    }
}
static void cpu_clear(uint32_t fb,unsigned sel,unsigned first)
{
    if(first){cpu_zero(fb+120U*2560U+640U,640U,480U);garden_ui_stars(320,120,640,480);return;}
    for(unsigned row=0;row<30U;row++){
        uint64_t mask=BH->masks[sel][row];
        for(unsigned x=0;x<40U;){
            if(!(mask&(UINT64_C(1)<<x))){x++;continue;}
            unsigned start=x++;while(x<40U && (mask&(UINT64_C(1)<<x)))x++;
            cpu_zero(fb+(120U+row*16U)*2560U+(320U+start*16U)*2U,(x-start)*16U,16U);
            garden_ui_stars(320+(int)start*16,120+(int)row*16,(int)(x-start)*16,16);
        }
    }
}
static void mark(unsigned sel,unsigned x,unsigned y)
{
    unsigned a=x/16U,b=(x+15U)/16U;uint64_t m=(UINT64_C(1)<<a)|(UINT64_C(1)<<b);
    BH->masks[sel][y/16U]|=m;BH->masks[sel][(y+15U)/16U]|=m;
}
static void cpu_history(uint32_t fb,unsigned sel,const PureBulletBuildResult *b,const PureBulletDesc *d)
{
    memset(BH->masks[sel],0,sizeof(BH->masks[sel]));
    for(unsigned tile=0;tile<27U;tile++)for(unsigned i=0;i<b->legacy_count[tile];i++){
        uint32_t off=BS->legacy[tile*PURE_SCATTER_BUCKET+i].dst-fb;
        mark(sel,(off%2560U)/2U-320U,off/2560U-120U);
    }
    for(unsigned i=0;i<b->s9_count;i++)mark(sel,(unsigned)d[i].x-320U,(unsigned)d[i].y-120U);
}
__attribute__((optimize("O3"))) static void cpu_sprite(uint32_t address,unsigned tile)
{
    cpu_raster(G9_ATLAS0_ADDR+tile*512U,address,16U,16U,32U,2560U);
}
static int hw_bottom(uint32_t fb)
{
    int rc=gpu_wait_idle(B_TIMEOUT);if(rc)return rc;
    GPU_DST_ADDR=fb+464U*2560U+640U;GPU_WIDTH=640U;GPU_HEIGHT=136U;GPU_DST_STRIDE=2560U;GPU_COLOR=0U;
    asm volatile("fence iorw,iorw" ::: "memory");GPU_CTRL=0U;GPU_CTRL=GPU_CTRL_FILL;return GPU_OK;
}
static int draw_legacy(uint32_t fb,unsigned count,const PureBulletBuildResult *b,unsigned cpu)
{
    if(!cpu){
        unsigned rows=count>=2093U && count<=2639U?80U:91U;
        int rc=gpu_fill(fb+120U*2560U+640U,640U,rows,2560U,0U);if(rc)return rc;
        garden_ui_stars(320,120,640,rows);garden_ui_stars(320,464,640,136);
    }
    for(unsigned tile=0;tile<27U;tile++){
        unsigned n=b->legacy_count[tile];if(n>256U)return GPU_EINVAL;
        GpuBlitEntry *list=BS->legacy+tile*256U;
        if(cpu){for(unsigned i=0;i<n;i++){cpu_sprite(list[i].dst,tile);if(!(i&31U))garden_log_pump();}}
        else if(n){int rc=gpu_bitblt_batch(list,n,G9_ATLAS0_ADDR+tile*512U,16,16,32,2560,mode_key,mode_alpha);if(rc)return rc;}
    }
    return GPU_OK;
}
typedef struct {
    Stage9LinkStats s9;
    DisplayDebugTotals display;
    unsigned front,drop,trunc;
} SixHealth;
static void six_record(const char *tag,unsigned cycle,unsigned case_id,
                       unsigned backend,unsigned mode,unsigned count,
                       uint64_t span,unsigned presented,uint32_t maximum,
                       uint64_t draw_ticks,uint64_t clear_ticks,
                       unsigned visible_min,unsigned visible_max,
                       const SixHealth *first,uint64_t prep_ticks)
{
    Stage9LinkStats s9;stage9_link_snapshot(&s9);
    const DisplayDebugTotals *d=display_debug_totals();
    const GardenLogStats *logs=garden_log_stats();
    unsigned fps=span?(unsigned)((uint64_t)presented*BSP_CLINT_HZ*1000U/span):0U;
    garden_log_printf("[%s] cycle=%u case=%u backend=%u mode=%u count=%u ticks=%u hz=%u presented=%u fps1000=%u mean_us=%u max_us=%u draw_us=%u clear_us=%u s9_engine_cycles=%u alpha=%u key=0x%x comparator_sw=%u blend_sw=%u common_source_crc=0x%x visible=%u visible_min=%u visible_max=%u missing_px=%u missing_line=%u reject=%u skip=%u errors=%u guard=%u front_reject=%u log_dropped=%u log_truncated=%u disp_px=%u disp_line=%u disp_full=%u prep_ticks=%u warmup=3 health_includes_prep=1\r\n",
        tag,cycle,case_id,backend,mode,count,(uint32_t)span,BSP_CLINT_HZ,presented,fps,
        presented?microseconds(span/presented):0U,microseconds(maximum),
        presented?microseconds(draw_ticks/presented):0U,
        backend&&presented?microseconds(clear_ticks/presented):0U,s9.last_frame_cycles,
        mode_alpha,mode_key,backend,backend&&mode_alpha<32U,runtime_source_crc,visible_max,visible_min,visible_max,
        s9.missing_px-first->s9.missing_px,s9.missing_lines-first->s9.missing_lines,
        s9.rejected-first->s9.rejected,s9.skipped-first->s9.skipped,s9.errors-first->s9.errors,
        guards()+g9_memory_guard_check(),display_stats()->front_rejects-first->front,
        logs->dropped_lines-first->drop,logs->truncated_lines-first->trunc,
        (unsigned)(d->px-first->display.px),(unsigned)(d->lines-first->display.lines),
        (unsigned)(d->full-first->display.full),(uint32_t)prep_ticks);
}
int modes_run(unsigned backend,unsigned mode,unsigned count,unsigned seconds)
{
    /* This independent showcase intentionally has one fixed workload. It is
     * not an entry to the normal game or to the old simultaneous compositor. */
    if(backend>1U || mode>2U || count!=2093U || seconds!=10U)return GPU_EINVAL;
    unsigned case_id=mode*2U+(backend?1U:2U),cycle=run_serial++/6U;
    uint64_t preparing=tick();
    int rc=stage9_link_init();if(rc)return rc;
    SixHealth first={0};stage9_link_snapshot(&first.s9);display_debug_sample();
    first.display=*display_debug_totals();first.front=display_stats()->front_rejects;
    first.drop=garden_log_stats()->dropped_lines;first.trunc=garden_log_stats()->truncated_lines;
    mode_alpha=mode==0U?32U:16U;
    mode_key=mode==1U?B_NOKEY:0xF81FU;
    if(backend){
        rc=gpu_wait_idle(B_TIMEOUT);if(rc)return rc;
        GPU_CTRL=0U;GPU_ALPHA=mode_alpha;
    }
    stage9_link_alpha(mode_alpha);stage9_link_key((uint16_t)mode_key);
    for(unsigned i=0;i<4U;i++){
        BS->before[i]=0x4D4F1010U+i;BS->after[i]=0x4D4F1020U+i;
        BH->before[i]=0x4D4F1030U+i;BH->after[i]=0x4D4F1040U+i;
    }
    memset(BH->masks,0,sizeof(BH->masks));unsigned index=0U;
    for(unsigned wave=0;wave<count/91U;wave++)for(unsigned ray=0;ray<91U;ray++)BS->plans[index++]=(PureBulletPlan){(uint16_t)ray,(uint16_t)(wave+1U)};
    pure_bullet_test_model_init(BS->p,count,BS->plans,B_RAYS,PURE_SPEED_DEFAULT_Q8);
    for(unsigned bank=0;bank<2U;bank++){
        volatile uint16_t *p=(volatile uint16_t *)(uintptr_t)(bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR);
        /* Both backends receive original, unmodified source pixels. Padding
         * is always the verified absent colour4, regardless of mode key. */
        for(unsigned i=0;i<G9_ATLAS_BYTES/2U;i++){
            p[i]=i<6912U?B_PIXELS[i]:B_NOKEY;
        }
    }
    publish();runtime_source_crc=bytes_crc((const volatile uint8_t *)(uintptr_t)G9_ATLAS0_ADDR,6912U*2U);
    uint32_t second_source_crc=bytes_crc((const volatile uint8_t *)(uintptr_t)G9_ATLAS1_ADDR,6912U*2U);
    runtime_atlas_crc[0]=bytes_crc((const volatile uint8_t *)(uintptr_t)G9_ATLAS0_ADDR,G9_ATLAS_BYTES);
    runtime_atlas_crc[1]=bytes_crc((const volatile uint8_t *)(uintptr_t)G9_ATLAS1_ADDR,G9_ATLAS_BYTES);
    if(runtime_source_crc!=B_COMMON_CRC || second_source_crc!=B_COMMON_CRC ||
       runtime_atlas_crc[0]!=B_ATLAS_CRC || runtime_atlas_crc[1]!=B_ATLAS_CRC){rc=GPU_EHW;goto failed;}
    LiveHud hud={0};
    rc=header(backend,mode,count,case_id,&hud);if(rc)goto failed;
    atlas_record(cycle,case_id,backend);
    garden_log_printf("[SIXL_TASK] cycle=%u case=%u backend=%u mode=%u count=%u alpha=%u key=0x%x alpha_enabled=%u key_effect=%u comparator_sw=%u blend_sw=%u common_source_crc=0x%x common_atlas_crc=0x%x converted_pixels=0 original_key_pixels=5142 original_black_pixels=4 no_key_matches=0 comparable_features=1 source_asymmetry=0 pixel_rules_differ=0 tile=16x16 rays=91 speed_q8=612 distribution=fan180 logic=0 independent=1 hud=common_cpu_live_1s clear=%s painter=legacy_then_s9\r\n",
        cycle,case_id,backend,mode,count,mode_alpha,mode_key,mode!=0U,mode!=1U,backend,backend&&mode!=0U,
        runtime_source_crc,runtime_atlas_crc[0],backend?"CPU_DIRTY_ALL_SPRITES":"ORIGINAL_T_ASYNC_BANDS");
    unsigned frame=0U,bank=0U,dirty[2]={0U,0U},total=0U,win_n=0U,win_max=0U,total_max=0U;
    unsigned win_visible_min=UINT32_MAX,win_visible_max=0U;
    unsigned visible_min=UINT32_MAX,visible_max=0U;
    uint64_t start=0U,last=0U,window=0U,prep=0U;
    uint64_t win_draw=0U,win_clear=0U,total_draw=0U,total_clear=0U;
    for(;;){
        render_begin();unsigned sel=display_back_sel();uint32_t fb=display_back_addr();
        if(display_check_render_target(fb)!=GPU_OK){rc=GPU_ESTATE;goto failed;}
        if(dirty[sel]){
            int pending=live_hud(backend,&hud,dirty[sel],backend?2U:1U);
            if(pending<0){rc=pending;goto failed;}dirty[sel]=(unsigned)pending;
        }
        if(!backend){rc=hw_bottom(fb);if(rc)goto failed;}
        bank^=1U;uint32_t da=bank?G9_DESC1_ADDR:G9_DESC0_ADDR;
        PureBulletDesc *d=(PureBulletDesc *)(uintptr_t)da;PureBulletBuildResult b;
        rc=pure_bullet_test_model_build(BS->p,d,&BS->legacy[0].dst,count,fb,BS->plans,B_RAYS,PURE_SPEED_DEFAULT_Q8,&b);
        if(rc || b.visible!=count){rc=GPU_ESTATE;goto failed;}
        uint64_t clear_start=tick();uint32_t clear_ticks=0U;
        if(backend){cpu_clear(fb,sel,frame<2U);clear_ticks=(uint32_t)(tick()-clear_start);cpu_history(fb,sel,&b,d);}
        else{rc=gpu_wait_idle(B_TIMEOUT);if(rc)goto failed;publish();}
        uint64_t draw_start=tick();rc=draw_legacy(fb,count,&b,backend);if(rc)goto failed;
        if(backend)for(unsigned i=0;i<b.s9_count;i++){
            cpu_sprite(fb+(unsigned)d[i].y*2560U+(unsigned)d[i].x*2U,d[i].sprite);
            if(!(i&31U))garden_log_pump();
        }
        uint32_t draw_ticks=(uint32_t)(tick()-draw_start);publish();
        if(!backend){
            rc=stage9_link_present(bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR,da,b.s9_count,fb,sel);if(rc)goto failed;
            if(*(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x120U)!=mode_key ||
               *(volatile uint32_t *)(uintptr_t)(GPU_BASE+0x124U)!=(mode_alpha==32U?32U:0x110U)){rc=GPU_ESTATE;goto failed;}
        }
        /* No framebuffer writes are legal below this point. */
        garden_log_pump();rc=render_end(B_TIMEOUT);if(rc)goto failed;
        uint64_t done=tick();frame++;display_debug_sample();
        if(guards() || g9_memory_guard_check() || display_stats()->front_rejects!=first.front){rc=GPU_EOVERLAP;goto failed;}
        if(frame<=2U)garden_log_printf("[SIXL_SEQUENCE] cycle=%u case=%u backend=%u mode=%u frame=%u checksum=0x%x visible=%u\r\n",cycle,case_id,backend,mode,frame,b.checksum,b.visible);
        if(frame==3U){
            start=last=window=done;prep=done-preparing;
            garden_log_printf("[SIXL_START] cycle=%u case=%u backend=%u mode=%u count=%u prep_ticks=%u formal_start_hi=%u formal_start_lo=%u hz=%u warmup=3\r\n",cycle,case_id,backend,mode,count,(uint32_t)prep,(uint32_t)(done>>32),(uint32_t)done,BSP_CLINT_HZ);
        }else if(frame>3U){
            uint64_t period=done-last;
            if(period>UINT32_MAX || !period){rc=GPU_ESTATE;goto failed;}
            total++;win_n++;total_draw+=draw_ticks;win_draw+=draw_ticks;
            total_clear+=clear_ticks;win_clear+=clear_ticks;
            if(period>win_max)win_max=(uint32_t)period;
            if(period>total_max)total_max=(uint32_t)period;
            if(b.visible<visible_min)visible_min=b.visible;
            if(b.visible>visible_max)visible_max=b.visible;
            if(b.visible<win_visible_min)win_visible_min=b.visible;
            if(b.visible>win_visible_max)win_visible_max=b.visible;
            last=done;
            if(done-window>=(uint64_t)BSP_CLINT_HZ || done-start>=(uint64_t)BSP_CLINT_HZ*seconds){
                uint64_t span=done-window;
                hud.live1000=(unsigned)((uint64_t)win_n*BSP_CLINT_HZ*1000U/span);
                hud.mean1000=(unsigned)((uint64_t)total*BSP_CLINT_HZ*1000U/(done-start));
                dirty[0]=dirty[1]=3U;
                six_record("SIXL_WINDOW",cycle,case_id,backend,mode,count,span,win_n,win_max,win_draw,win_clear,win_visible_min,win_visible_max,&first,prep);
                window=done;win_n=win_max=0U;win_draw=win_clear=0U;
                win_visible_min=UINT32_MAX;win_visible_max=0U;
            }
            if(done-start>=(uint64_t)BSP_CLINT_HZ*seconds){
                six_record("SIXL_DONE",cycle,case_id,backend,mode,count,done-start,total,total_max,total_draw,total_clear,visible_min,visible_max,&first,prep);
                rc=GPU_OK;break;
            }
        }
        garden_log_pump();
    }
    {int stopped=stage9_link_stop();if(!rc)rc=stopped;}
    garden_render_mode(0);garden_log_pump();return rc;
failed:
    garden_log_printf("[SIXL_FAILED] cycle=%u case=%u backend=%u mode=%u rc=%d guard=%u front_reject=%u log_dropped=%u log_truncated=%u\r\n",
        cycle,case_id,backend,mode,rc,guards()+g9_memory_guard_check(),display_stats()->front_rejects-first.front,
        garden_log_stats()->dropped_lines-first.drop,garden_log_stats()->truncated_lines-first.trunc);
    display_debug_report("SIXL_FAILED",0U);(void)stage9_link_stop();garden_log_pump();return rc;
}
