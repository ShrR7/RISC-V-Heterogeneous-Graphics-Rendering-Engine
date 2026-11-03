/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compile the real renderer and compare every RGB565 pixel in CPU/single/list
 * modes. Host mocks model ordered partial Alpha execution, not FPGA timing. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "garden_render.h"
#include "garden_assets.h"
#include "garden_ui.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"
#define FRAMES 96U
#define LIST_ADDR 0x0067D000U
static unsigned checks,sel,gpu_calls,single_blits,batch_calls,fill_batches,blit_batches,batch_entries;
static unsigned fail_batch,failed_entries,fake_busy,fail_fill,fill_failed_at;
static uint8_t failed_fill_list[2048],failed_blit_list[2048];
static unsigned fill_failed_count,blit_failed_count;
static unsigned expect_pair;
static uint32_t pair_dst[2];
static uint8_t *reference,*other;
#define CHECK(c,m) do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static uint8_t *fb(unsigned b){return (uint8_t *)(uintptr_t)(b?FB1_ADDR:FB0_ADDR);}
uint32_t display_back_sel(void){return sel;}
uint32_t display_back_addr(void){return sel?FB1_ADDR:FB0_ADDR;}
int display_check_render_target(uint32_t p){return p>=display_back_addr() && p<display_back_addr()+FB_SIZE_BYTES?GPU_OK:GPU_ESTATE;}
void garden_render_host_fence(void){}
void data_cache_invalidate_all(void){}
int gpu_busy(void){return (int)fake_busy;}
static void bounds(uint32_t p,unsigned w,unsigned h,unsigned stride)
{
    unsigned off=p-display_back_addr(),x=(off%DISPLAY_STRIDE_BYTES)/2U,y=off/DISPLAY_STRIDE_BYTES;
    CHECK(w && h && stride==DISPLAY_STRIDE_BYTES && x+w<=DISPLAY_WIDTH && y+h<=DISPLAY_HEIGHT && display_check_render_target(p)==GPU_OK,"only clipped back framebuffer");
    CHECK(w>128U?h<=16U:(h<=64U && w*h<=4096U),"unchanged per-instance row/pixel safety limits");
}
static void fill(uint32_t p,unsigned w,unsigned h,unsigned stride,uint16_t c)
{bounds(p,w,h,stride);for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)*(uint16_t *)(uintptr_t)(p+y*stride+x*2U)=c;}
int gpu_fill(uint32_t p,uint32_t w,uint32_t h,uint32_t st,uint16_t c)
{gpu_calls++;fill(p,w,h,st,c);return GPU_OK;}
static void blit(uint32_t s,uint32_t d,unsigned w,unsigned h,unsigned ss,unsigned ds,uint16_t k,unsigned a)
{
    bounds(d,w,h,ds);
    CHECK(s>=ASSET_BASE_ADDR && (uint64_t)s+(h-1U)*ss+w*2U<=ASSET_BASE_ADDR+ASSET_SIZE_BYTES && a<=32U,"source/alpha ranges");
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        uint16_t p=*(uint16_t *)(uintptr_t)(s+y*ss+x*2U),*q=(uint16_t *)(uintptr_t)(d+y*ds+x*2U);
        if(p!=k)*q=a==32U?p:gpu_alpha_rgb565(p,*q,a);
    }
}
int gpu_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k)
{gpu_calls++;single_blits++;blit(s,d,w,h,ss,ds,k,32U);return GPU_OK;}
int gpu_alpha_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{gpu_calls++;single_blits++;blit(s,d,w,h,ss,ds,k,a);return GPU_OK;}
int gpu_fill_batch(const GpuFillEntry *p,uint32_t n,uint32_t ds,uint16_t c)
{
    CHECK((uintptr_t)p==LIST_ADDR && n>1U && n<=256U,"fixed isolated aligned 256-entry fill list capacity");
    gpu_calls++;batch_calls++;fill_batches++;batch_entries+=n;
    if(fail_fill){fill(p[0].dst,p[0].width,p[0].height,ds,c);fail_fill=0;fake_busy=1;fill_failed_at=gpu_calls;fill_failed_count=n;memcpy(failed_fill_list,p,sizeof(failed_fill_list));return GPU_ETIMEOUT;}
    for(unsigned i=0;i<n;i++)fill(p[i].dst,p[i].width,p[i].height,ds,c);
    return GPU_OK;
}
int gpu_bitblt_batch(const GpuBlitEntry *p,uint32_t n,uint32_t s,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{
    CHECK((uintptr_t)p==LIST_ADDR && n>1U && n<=256U,"fixed isolated aligned blit list capacity");
    gpu_calls++;batch_calls++;blit_batches++;batch_entries+=n;
    if(expect_pair){CHECK(n==2U && p[0].dst==pair_dst[0] && p[1].dst==pair_dst[1],"second Blit item materializes ordered first/second DDR destinations");expect_pair=0;}
    unsigned count=fail_batch?n/2U:n;
    for(unsigned i=0;i<count;i++)blit(s,p[i].dst,w,h,ss,ds,k,a);
    if(fail_batch){blit_failed_count=n;memcpy(failed_blit_list,p,sizeof(failed_blit_list));unsigned timeout=fail_batch==2U;fail_batch=0;failed_entries=count;fake_busy=timeout;return timeout?GPU_ETIMEOUT:GPU_EHW;}
    return GPU_OK;
}
static unsigned sequence(unsigned f,GardenDraw *d)
{
    unsigned n=0;
    if(f%12U==11U)return 0;
    /* Dense contiguous repeats exercise a real list, including overlapping
     * Alpha destinations where sorting/replaying changes final RGB565. */
    unsigned count=f%12U==0U?240U:64U;
    for(unsigned i=0;i<count;i++){
        unsigned id=(f%4U==0U)?GARDEN_ASSET_SHOT:(f%4U==1U)?GARDEN_ASSET_CIRCLE_RED:(f%4U==2U)?GARDEN_ASSET_DARTER1:GARDEN_ASSET_HIT_SPARK;
        unsigned a=f%3U==0U?32U:16U;
        int x=(int)(i%16U)*35+(int)(f%7U)-4,y=(int)(i/16U)*24+7+(int)(f%9U);
        if(f%12U==3U){x=300+(int)(i%3U);y=220+(int)(i%5U);}
        d[n++]=(GardenDraw){(int16_t)x,(int16_t)y,(uint16_t)id,(uint8_t)a};
    }
    if(n==240U)return n;
    /* Interleaved different sources/alphas and rectangle composites are
     * barriers. These include alpha0, clipping on all field boundaries,
     * bank bands, forming cores, health strips, damage and repeating beams. */
    d[n++]=(GardenDraw){319,220,GARDEN_ASSET_BOSS1,24};
    d[n++]=(GardenDraw){321,221,GARDEN_ASSET_FLASH_BOSS1,12};
    d[n++]=(GardenDraw){320,220,GARDEN_ASSET_BOSS1,16};
    d[n++]=(GardenDraw){1,1,GARDEN_ASSET_STOPPER1,32};
    d[n++]=(GardenDraw){639,479,GARDEN_ASSET_PLAYER,8};
    d[n++]=(GardenDraw){100,100,GARDEN_ASSET_PLAYER,0};
    d[n++]=(GardenDraw){100,101,GARDEN_ASSET_PLAYER,32};
    d[n++]=(GardenDraw){200,280,GARDEN_DRAW_BANK_BASE|((f%5U)<<2)|(f%3U),24};
    d[n++]=(GardenDraw){285,170,GARDEN_DRAW_BAR_BASE|GARDEN_DRAW_BAR_BOSS|(f%127U),32};
    d[n++]=(GardenDraw){300,145,GARDEN_DRAW_SCORE_BASE|95,16};
    d[n++]=(GardenDraw){300,143,GARDEN_DRAW_SCORE_BASE|190,24};
    d[n++]=(GardenDraw){200,230,GARDEN_DRAW_FORM_BASE|GARDEN_DRAW_FORM_GROW|GARDEN_ASSET_DART_BASE,8};
    if(f%4U==0U)d[n++]=(GardenDraw){390,150,GARDEN_DRAW_LASER_BASE|GARDEN_DRAW_LASER_ACTIVE|((f%16U)<<8)|13U,16};
    return n;
}
static void external_hud(unsigned f)
{
    garden_render_box(-2,700,160,8,0x1084);
    garden_render_text_style(10,700,f%2U?"BATCH TEST":"SINGLE RGB",GARDEN_TEXT_SMALL_WHITE);
    garden_render_box(1278,-2,8,8,0xFFFF);
}
static void reset(unsigned mode)
{
    memset(fb(0),0x6b,FB_SIZE_BYTES);memset(fb(1),0x3d,FB_SIZE_BYTES);
    garden_render_init();garden_render_alpha_support(1);garden_render_mode(mode==0U);
    garden_render_batch_mode(mode==2U);
    CHECK(garden_render_is_cpu()==(mode==0U) && garden_render_is_batch()==(mode==2U),"independent CPU/single/batch switch");
}
static void exact_frames(void)
{
    GardenDraw d[GARDEN_DRAW_MAX];unsigned single_submissions=0,batch_submissions=0,observed_fill=0,observed_blit=0;
    for(unsigned mode=0;mode<3;mode++){
        reset(mode);gpu_calls=batch_calls=fill_batches=blit_batches=batch_entries=single_blits=0;
        for(unsigned f=0;f<FRAMES;f++){
            sel=f&1U;memcpy(other,fb(sel^1U),FB_SIZE_BYTES);
            if(f==48U)garden_render_alpha_toggle();
            if(f==64U)garden_render_alpha_toggle();
            garden_render_panel_visible(f>=24U && f<32U && f%12U!=0U);
            unsigned n=sequence(f,d);
            int rc=garden_render_frame(d,n);
            if(rc!=GPU_OK) fprintf(stderr,"mode=%u frame=%u n=%u rc=%d err=%u target=%u atlas=%u\n",mode,f,n,rc,garden_render_stats()->errors,garden_render_stats()->bad_targets,garden_render_stats()->atlas_bounds);
            CHECK(rc==GPU_OK,"real production frame returns success");
            CHECK(!memcmp(other,fb(sel^1U),FB_SIZE_BYTES),"other/front physical buffer unchanged");
            external_hud(f);
            if(mode==0U)memcpy(reference+(size_t)f*FB_SIZE_BYTES,fb(sel),FB_SIZE_BYTES);
            else CHECK(!memcmp(reference+(size_t)f*FB_SIZE_BYTES,fb(sel),FB_SIZE_BYTES),"complete RGB565 CPU/single/batch differential, both dirty histories");
            CHECK(garden_render_stats()->errors==0 && garden_render_stats()->bad_targets==0 && garden_render_stats()->atlas_bounds==0,"no hidden renderer validation errors");
        }
        if(mode==1U){single_submissions=gpu_calls;CHECK(batch_calls==0,"single mode never submits lists");}
        if(mode==2U){batch_submissions=gpu_calls;observed_fill=fill_batches;observed_blit=blit_batches;
            CHECK(batch_calls==garden_render_stats()->batch_commands && batch_entries==garden_render_stats()->batch_instances,"list statistics count submissions and instances");
            CHECK(fill_batches==garden_render_stats()->batch_fill_commands && blit_batches==garden_render_stats()->batch_blit_commands,"per-operation batch stats");}
    }
    CHECK(observed_fill>0 && observed_blit>0 && batch_submissions<single_submissions,"actual fill/blit lists reduce GPU starts without changing pixels");
    printf("RGB frames=%u comparisons=%u pixels_per_frame=%u single_submissions=%u batch_submissions=%u fill_lists=%u blit_lists=%u\n",FRAMES,2U*FRAMES,DISPLAY_WIDTH*DISPLAY_HEIGHT,single_submissions,batch_submissions,observed_fill,observed_blit);
}
static void singleton_no_ddr(void)
{
    GardenDraw d;
    DWORD old_protect=0,discard=0;
    /* PAGE_NOACCESS proves both no reads AND no writes to any list byte.
     * Initial static UI is completed first, outside the batch scope. */
    for(unsigned op=0;op<3U;op++){
        reset(2);for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"init singleton DDR guard buffers");
        sel=0;
        d=op==2U?(GardenDraw){200,200,GARDEN_DRAW_BAR_BASE|20U,32}:
                    (GardenDraw){200,200,GARDEN_ASSET_SHOT,(uint8_t)(op==1U?16U:32U)};
        unsigned lists=batch_calls;
        CHECK(VirtualProtect((void *)(uintptr_t)LIST_ADDR,4096U,PAGE_NOACCESS,&old_protect),"protect entire DDR command list page");
        CHECK(garden_render_frame(&d,1)==GPU_OK,"singleton opaque/Alpha Blit and differently colored Fill never access DDR list");
        CHECK(VirtualProtect((void *)(uintptr_t)LIST_ADDR,4096U,old_protect,&discard),"restore host-only list page protection");
        CHECK(batch_calls==lists,"singletons submit only old GPU commands");
    }
    reset(2);for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"init two-item materialization buffers");
    sel=0;const garden_sprite_t *asset=garden_asset(GARDEN_ASSET_SHOT);
    GardenDraw pair[2]={{200,200,GARDEN_ASSET_SHOT,16},{240,200,GARDEN_ASSET_SHOT,16}};
    for(unsigned i=0;i<2U;i++)pair_dst[i]=display_back_addr()+
        (GARDEN_Y0+200U-asset->anchor_y)*DISPLAY_STRIDE_BYTES+
        (GARDEN_X0+(unsigned)pair[i].x-asset->anchor_x)*2U;
    expect_pair=1;
    CHECK(garden_render_frame(pair,2)==GPU_OK && !expect_pair,"two compatible Blits use exactly ordered materialized list entries");
}

static void partial_failure(void)
{
    reset(2);for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"init failure test buffers");
    sel=0;memcpy(other,fb(1),FB_SIZE_BYTES);
    GardenDraw d[20];for(unsigned i=0;i<20;i++)d[i]=(GardenDraw){300,240,GARDEN_ASSET_HIT_SPARK,16};
    gpu_calls=single_blits=batch_calls=0;fail_batch=1;
    CHECK(garden_render_frame(d,20)==GPU_EHW,"partial list hardware error reaches frame caller");
    CHECK(batch_calls==1U && gpu_calls==1U && single_blits==0U && failed_entries==10U,"consume failed list once; no single fallback or replay");
    CHECK(!memcmp(other,fb(1),FB_SIZE_BYTES),"failed list cannot damage front buffer");
    unsigned count=gpu_calls;garden_render_batch_mode(0);garden_render_mode(1);garden_render_mode(0);
    CHECK(gpu_calls==count,"mode switches do not replay failed consumed queue");
    CHECK(garden_render_frame(NULL,0)==GPU_OK,"dirty history retained for failed partial frame");
    /* Fresh empty static reference must equal a fully erased failure frame. */
    memcpy(other,fb(0),FB_SIZE_BYTES);sel=1;
    CHECK(!memcmp(other,fb(1),FB_SIZE_BYTES),"failed partial pixels fully erased next same-buffer frame");
}
static void timeout_holds_list(void)
{
    reset(2);for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"init timeout test buffers");
    sel=0;GardenDraw d[20];for(unsigned i=0;i<20;i++)d[i]=(GardenDraw){(int16_t)(260+i),240,GARDEN_ASSET_HIT_SPARK,16};
    gpu_calls=single_blits=batch_calls=0;fail_batch=2;
    CHECK(garden_render_frame(d,20)==GPU_EHW && fake_busy,"timeout leaves hardware running and frame failed");
    uint8_t saved_list[2048];memcpy(saved_list,(void *)(uintptr_t)LIST_ADDR,sizeof(saved_list));
    memcpy(other,fb(0),FB_SIZE_BYTES);unsigned before=gpu_calls;
    garden_render_batch_mode(0);garden_render_mode(1);
    garden_render_box(100,100,20,20,0xFFFF);garden_render_alpha_support(1);
    CHECK(garden_render_key_probe()==GPU_EHW,"readback probe does not bypass busy fault");
    CHECK(garden_render_frame(d,20)==GPU_EHW,"next frame blocked while timed-out GPU stays busy");
    garden_render_init();
    CHECK(garden_render_frame(NULL,0)==GPU_EHW,"reinitialization cannot release an in-flight list");
    CHECK(gpu_calls==before && !memcmp(other,fb(0),FB_SIZE_BYTES) && !memcmp(saved_list,(void *)(uintptr_t)LIST_ADDR,sizeof(saved_list)),"busy timeout preserves framebuffer/list and blocks all new submissions");
    fake_busy=0;garden_render_mode(0);
    CHECK(garden_render_frame(NULL,0)==GPU_OK,"host idle recovery erases partials without replay");
}

static void midframe_fill_timeout(void)
{
    reset(2);for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"init mid-fill failure buffers");
    sel=0;GardenDraw d[64];for(unsigned i=0;i<64;i++)d[i]=(GardenDraw){(int16_t)(25+(i%8U)*75U),(int16_t)(30+(i/8U)*50U),GARDEN_ASSET_BOSS1,32};
    CHECK(garden_render_frame(d,64)==GPU_OK,"seed separated dirty rects for bounded Fill lists");
    gpu_calls=fill_failed_at=0;fail_fill=1;
    CHECK(garden_render_frame(NULL,0)==GPU_EHW && !fail_fill && fill_failed_at>0U,"mid-erase Fill list partial timeout is observed");
    CHECK(gpu_calls==fill_failed_at,"no later command submitted after mid-frame failed flush");
    CHECK(!memcmp(failed_fill_list,(void *)(uintptr_t)LIST_ADDR,sizeof(failed_fill_list)),"submit does not overwrite first list entry after failed capacity flush");
    CHECK(garden_render_frame(NULL,0)==GPU_EHW && gpu_calls==fill_failed_at,"next frame cannot reuse in-flight failed Fill list");
    fake_busy=0;garden_render_init();
}

int main(int argc,char **argv)
{
    CHECK(argc==2,"atlas path required");
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"map real board addresses in host RAM");
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL,"actual atlas file");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES && fgetc(f)==EOF,"exact frozen atlas");fclose(f);
    reference=malloc((size_t)FRAMES*FB_SIZE_BYTES);other=malloc(FB_SIZE_BYTES);CHECK(reference && other,"host-only full frame references");
    exact_frames();singleton_no_ddr();partial_failure();timeout_holds_list();midframe_fill_timeout();
    printf("PASS batch_render checks=%u full_rgb565_frames=%u comparison_frames=%u frame_bytes=%u list_addr=0x%08X list_bytes=2048 failure_no_replay=1 lazy_singleton_no_ddr=1\n",checks,3U*FRAMES,2U*FRAMES,FB_SIZE_BYTES,LIST_ADDR);
    free(reference);free(other);VirtualFree(ram,0,MEM_RELEASE);return 0;
}

