/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production score renderer/HUD RGB565 pixel contracts. GPU command callbacks
 * and a fixed-address host RAM mapping replace hardware; no timing claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "garden_render.h"
#include "garden_assets.h"
#include "garden_ui.h"
#include "garden_actor_ui.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_ref.h"

static unsigned checks,sel,scope,fail_glyph,fail_box,reject_target;
static unsigned calls,glyph_calls,fill_calls,fences;
typedef struct { uint32_t src,dst,w,h,ss,ds; unsigned alpha; } BlitTrace;
typedef struct { uint32_t dst,w,h; uint16_t color; } FillTrace;
static BlitTrace trace_blits[256];
static FillTrace trace_fills[256];
static unsigned trace_on,trace_blit_count,trace_fill_count,fail_at_glyph;
static uint16_t expected[DISPLAY_HEIGHT][DISPLAY_WIDTH],base[DISPLAY_HEIGHT][DISPLAY_WIDTH];
static uint16_t old_pixels[DISPLAY_HEIGHT][DISPLAY_WIDTH];
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while (0)

static uint16_t *fb(unsigned b) { return (uint16_t *)(uintptr_t)(b ? FB1_ADDR:FB0_ADDR); }
uint32_t display_back_sel(void) { return sel; }
uint32_t display_back_addr(void) { return sel ? FB1_ADDR:FB0_ADDR; }
int display_check_render_target(uint32_t address)
{
    if (reject_target) return GPU_ESTATE;
    uint32_t begin=display_back_addr();
    return address>=begin && address<begin+FB_SIZE_BYTES ? GPU_OK:GPU_ESTATE;
}
void garden_render_host_fence(void) { fences++; }
void data_cache_invalidate_all(void) {}

static void command_bounds(uint32_t dst,unsigned w,unsigned h,unsigned stride)
{
    uint32_t offset=dst-display_back_addr();
    unsigned x=(offset%DISPLAY_STRIDE_BYTES)/2U,y=offset/DISPLAY_STRIDE_BYTES;
    CHECK(stride==DISPLAY_STRIDE_BYTES && h<=16U,"production GPU operations use display stride and watchdog-safe bands <=16");
    CHECK(display_check_render_target(dst)==GPU_OK && x+w<=DISPLAY_WIDTH && y+h<=DISPLAY_HEIGHT,
          "every command stays in the known physical back buffer");
    if(scope==1U) CHECK(x>=GARDEN_X0 && y>=GARDEN_Y0 && x+w<=GARDEN_X0+GARDEN_WIDTH && y+h<=GARDEN_Y0+GARDEN_HEIGHT,
                         "score composites/history touch only logical field");
    if(scope==2U) CHECK(x>=56U && y>=220U && x+w<=264U && y+h<=236U,
                         "fast score API touches only 208x16 score value window");
}

int gpu_fill(uint32_t dst,uint32_t w,uint32_t h,uint32_t stride,uint16_t color)
{
    command_bounds(dst,w,h,stride); calls++;fill_calls++;
    if(trace_on) {
        CHECK(trace_fill_count<256U,"bounded command recording");
        trace_fills[trace_fill_count++]=(FillTrace){dst,w,h,color};
    }
    if(fail_box) { fail_box=0;return GPU_EHW; }
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++)
        *(uint16_t *)(uintptr_t)(dst+y*stride+x*2U)=color;
    return GPU_OK;
}
static int blit(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t key,unsigned alpha)
{
    command_bounds(dst,w,h,ds);calls++;glyph_calls++;
    CHECK(src>=ASSET_BASE_ADDR && (uint64_t)src+(h-1U)*ss+w*2U<=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES,
          "actual glyph source stays inside unchanged atlas");
    CHECK(alpha<=32U && !(src&1U) && !(dst&1U),"Alpha/even-address command contract");
    if(trace_on) {
        CHECK(trace_blit_count<256U,"bounded glyph command recording");
        trace_blits[trace_blit_count++]=(BlitTrace){src,dst,w,h,ss,ds,alpha};
    }
    if(fail_at_glyph && !--fail_at_glyph) return GPU_EHW;
    if(fail_glyph) { fail_glyph=0;return GPU_EHW; }
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint16_t v=*(uint16_t *)(uintptr_t)(src+y*ss+x*2U);
        uint16_t *out=(uint16_t *)(uintptr_t)(dst+y*ds+x*2U);
        if(v!=key) *out=alpha==32U ? v:gpu_alpha_rgb565(v,*out,alpha);
    }
    return GPU_OK;
}
int gpu_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k)
{ return blit(s,d,w,h,ss,ds,k,32U); }
int gpu_alpha_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{ return blit(s,d,w,h,ss,ds,k,a); }

static void ref_box(int x,int y,int w,int h,uint16_t color)
{
    for(int row=0;row<h;row++) for(int col=0;col<w;col++) {
        int px=x+col,py=y+row;
        if(px>=GARDEN_X0 && px<GARDEN_X0+GARDEN_WIDTH && py>=GARDEN_Y0 && py<GARDEN_Y0+GARDEN_HEIGHT)
            expected[py][px]=color;
    }
}
static void ref_sprite(unsigned id,int x,int y,unsigned alpha)
{
    const garden_sprite_t *s=garden_asset(id);CHECK(s!=NULL,"oracle uses real atlas descriptor");
    x-=s->anchor_x;y-=s->anchor_y;
    for(unsigned row=0;row<s->height;row++) for(unsigned col=0;col<s->width;col++) {
        int px=x+(int)col,py=y+(int)row;
        if(px<GARDEN_X0 || px>=GARDEN_X0+GARDEN_WIDTH || py<GARDEN_Y0 || py>=GARDEN_Y0+GARDEN_HEIGHT) continue;
        uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+row*s->stride+col*2U);
        if(v!=GARDEN_KEY_COLOR) expected[py][px]=alpha==32U ? v:gpu_alpha_rgb565(v,expected[py][px],alpha);
    }
}
static void ref_bank(unsigned value,int x,int y,unsigned alpha)
{
    static const int shifts[5][6]={{-4,-3,-1,0,2,3},{-2,-1,0,0,1,1},{0,0,0,0,0,0},
                                  {2,1,0,0,-1,-1},{4,3,1,0,-2,-3}};
    static const unsigned ends[6]={4,9,14,19,24,29};
    unsigned pose=(value>>2)&7U,frame=value&3U;
    unsigned id=frame ? GARDEN_ASSET_PLAYER_FRAME1+frame-1U:GARDEN_ASSET_PLAYER;
    const garden_sprite_t *asset=garden_asset(id);
    CHECK(asset && asset->width==34 && asset->height==29 && asset->anchor_x==17 && asset->anchor_y==15,
          "independent bank oracle is bound to unchanged real 34x29 player art");
    for(unsigned row=0;row<29;row++) {
        unsigned band=0;while(row>=ends[band]) band++;
        for(unsigned col=0;col<34;col++) {
            int px=x+(int)col-17+shifts[pose][band],py=y+(int)row-15;
            if(px<GARDEN_X0 || px>=GARDEN_X0+GARDEN_WIDTH || py<GARDEN_Y0 || py>=GARDEN_Y0+GARDEN_HEIGHT) continue;
            uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+asset->offset+row*asset->stride+col*2U);
            if(v!=GARDEN_KEY_COLOR) expected[py][px]=alpha==32U ? v:gpu_alpha_rgb565(v,expected[py][px],alpha);
        }
    }
}
static void ref_bar(unsigned value,int x,int y,unsigned alpha)
{
    unsigned boss=(value&256U)!=0U,width=boss ? 128U:40U,fill=value&255U;
    uint16_t color=boss ? GARDEN_UI_CYAN:0x5FEAU,border=0xAD75U,empty=0x1084U;
    if(fill*3U<width-2U) color=0xFD20U;
    if(alpha<32U) { color=gpu_alpha_rgb565(color,0,alpha);border=gpu_alpha_rgb565(border,0,alpha);empty=gpu_alpha_rgb565(empty,0,alpha); }
    if(!alpha) return;
    /* Independent per-pixel classification, rather than production's three
     * rectangular fill commands; partial edge clipping happens in ref_box. */
    for(unsigned row=0;row<8U;row++) for(unsigned col=0;col<width;col++) {
        uint16_t v=row==0U || row==7U || col==0U || col==width-1U ? border:
                   col<=fill ? color:empty;
        ref_box(x+(int)col,y+(int)row,1,1,v);
    }
}
static void oracle_draw(const GardenDraw *d,unsigned alpha_on)
{
    unsigned alpha=alpha_on ? d->alpha:32U;
    int x=GARDEN_X0+d->x,y=GARDEN_Y0+d->y;
    if(!(d->asset&GARDEN_DRAW_SCORE_BASE)) {
        if(d->asset&GARDEN_DRAW_BAR_BASE) ref_bar(d->asset&~GARDEN_DRAW_BAR_BASE,x,y,alpha);
        else if(d->asset&GARDEN_DRAW_BANK_BASE) ref_bank(d->asset&~GARDEN_DRAW_BANK_BASE,x,y,alpha);
        else ref_sprite(d->asset,x,y,alpha);
        return;
    }
    unsigned v=d->asset&0x7FFFU;
    if(!v || !alpha) return;
    char decimal[8]; int n=snprintf(decimal,sizeof(decimal),"%u",v);
    CHECK(n>0 && n<=5,"virtual positive damage bounded to five digits");
    unsigned font=v==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE;
    uint16_t color=v==1U ? 0xFFFFU:GARDEN_UI_CYAN;
    if(alpha<32U) color=gpu_alpha_rgb565(color,0U,alpha);
    ref_box(x+2,y+7,8,2,color);
    for(int i=0;i<n;i++) {
        int id=garden_glyph(decimal[i]);CHECK(id>=0,"real score digit glyph exists");
        ref_sprite(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x+12+i*16,y,alpha);
    }
}

static void render_sequences(void)
{
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned on=0;on<2;on++) {
        garden_render_init();garden_render_alpha_support(1);
        if(!on) CHECK(garden_render_alpha_toggle()==GPU_OK,"Alpha-off fallback available");
        garden_render_mode(cpu);scope=0;
        for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"initialize both physical backgrounds");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0;tick<92U;tick++) for(unsigned b=0;b<2;b++) {
            sel=(tick+b)&1U;GardenDraw d[5];unsigned count=0;
            static const int px[9]={100,-2,640,0,635,400,100,640,100};
            static const int py[9]={100,100,478,-5,0,479,100,0,100};
            unsigned k=tick%9U,pose=tick%5U,frame=(tick/5U)%3U;
            unsigned alpha=tick%4U==0U ? 16U:tick%4U==1U ? 24U:32U;
            if(tick<90U) {
                d[count++]=(GardenDraw){(int16_t)px[k],(int16_t)py[k],(uint16_t)(0x2000U|(pose<<2)|frame),(uint8_t)alpha};
                d[count++]=(GardenDraw){(int16_t)(px[k]-20),(int16_t)(py[k]-35),(uint16_t)(0x4000U|(tick%39U)),32};
                d[count++]=(GardenDraw){(int16_t)(px[k]-64),(int16_t)(py[k]-70),(uint16_t)(0x4100U|(tick%127U)),(uint8_t)alpha};
                if(tick%2U==0U) d[count++]=(GardenDraw){(int16_t)(px[k]-10),(int16_t)(py[k]-12),0x805FU,(uint8_t)alpha};
                if(tick%3U==0U) d[count++]=(GardenDraw){(int16_t)px[k],(int16_t)py[k],GARDEN_ASSET_FOCUS_VULNERABLE,32};
            }
            memcpy(expected,base,sizeof(expected));
            for(unsigned i=0;i<count;i++) oracle_draw(&d[i],on);
            scope=1;CHECK(garden_render_frame(d,count)==GPU_OK,"production actor composite frame succeeds");scope=0;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"production CPU/GPU bank, bar and damage pixels match independent full oracle across both FB histories");
            CHECK(garden_render_stats()->sprites<=count,"one common history rectangle per bank/bar/label, never one per strip");
        }
        CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,
              "valid actor sequences never fail target/atlas checks");
    }
    puts("PASS: production five bank poses x three thruster frames, true anchor, HP bars, clipping, Alpha fallback and two-FB composite dirty erasure");
}
static void rejections(void)
{
    garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
    GardenDraw d={100,100,0x8000U,32};
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"zero virtual value rejected without pixels");
    d.asset=0x8001U;d.alpha=33;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"invalid score Alpha rejected without pixels");
    CHECK(garden_render_frame(NULL,1)==GPU_EINVAL,"non-empty null draw input rejected");
    sel=2;CHECK(garden_render_frame(NULL,0)==GPU_ESTATE,"unknown physical framebuffer rejected before history access");sel=0;
    d.alpha=32;reject_target=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"composite minus and digits enforce target guard before writing");reject_target=0;
    /* Clear failed frame history before injecting a glyph-only fault. */
    CHECK(garden_render_frame(NULL,0)==GPU_OK,"failed target history remains safely erasable");
    fail_glyph=1;CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_glyph,"actual score glyph GPU error propagates to frame result");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"failed composite retained one history rectangle and erases cleanly next frame");
    puts("PASS: zero value, invalid Alpha/null input/FB, front-target guard and GPU glyph-fault recovery");
}

static void actor_rejections(void)
{
    static const unsigned invalid[]={0x2013U,0x2014U,0x2020U,0x4080U,0x4180U,0x4200U,0x6000U};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
        GardenDraw d={100,100,(uint16_t)invalid[i],32};
        CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"malformed bank/bar domain rejected without pixels");
    }
    for(unsigned cpu=0;cpu<2;cpu++) for(unsigned kind=0;kind<2;kind++) {
        garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);garden_render_mode(cpu);memcpy(old_pixels,fb(0),sizeof(old_pixels));
        GardenDraw d={100,100,(uint16_t)(kind ? 0x4110U:0x2010U),32};reject_target=1;
        CHECK(garden_render_frame(&d,1)==GPU_EHW && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"bank/bar check every target before CPU or GPU writing");reject_target=0;
        CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"failed target composite cleanly erases history");
    }
    garden_render_init();sel=0;scope=0;garden_render_frame(NULL,0);memcpy(old_pixels,fb(0),sizeof(old_pixels));
    GardenDraw d={100,100,0x2010U,32};fail_glyph=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_glyph,"bank strip GPU failure propagated");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"partly painted bank strips retained in one dirty rect, erase without residual");
    d.asset=0x410FU;fail_box=1;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_box,"bar fill GPU failure propagated");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"partly painted bar erased without residual");
    puts("PASS: reserved bits/invalid bank poses/fill/Alpha and CPU/GPU target rejection; GPU strip/fill fault recovery");
}

/* Full-glyph oracle remains untrimmed. These tests derive ink bounds by
 * scanning actual source pixels, then independently inspect GPU commands. */
static void atlas_ink(unsigned id,unsigned *left,unsigned *top,unsigned *width,unsigned *height)
{
    const garden_sprite_t *a=garden_asset(id);
    CHECK(a && a->width==16U && a->height==16U && a->stride==32U && !a->anchor_x && !a->anchor_y,
          "trim contract binds unchanged16x16font,32byte source stride and original anchor");
    unsigned xmin=16U,ymin=16U,xmax=0U,ymax=0U,found=0U;
    for(unsigned y=0;y<16U;y++) for(unsigned x=0;x<16U;x++) {
        uint16_t v=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+a->offset+y*32U+x*2U);
        if(v==GARDEN_KEY_COLOR) continue;
        if(x<xmin) xmin=x;
        if(y<ymin) ymin=y;
        if(x>xmax) xmax=x;
        if(y>ymax) ymax=y;
        found=1U;
    }
    CHECK(found,"each decimal digit has actual non-Key pixels");
    *left=xmin;*top=ymin;*width=xmax-xmin+1U;*height=ymax-ymin+1U;
    for(unsigned y=0;y<16U;y++) for(unsigned x=0;x<16U;x++)
        if(x<xmin || x>xmax || y<ymin || y>ymax)
            CHECK(*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+a->offset+y*32U+x*2U)==GARDEN_KEY_COLOR,
                  "every omitted source-margin pixel is really Key, no scaling or lost font stroke");
}
static void trimmed_digits(void)
{
    for(unsigned digit=0;digit<10U;digit++) {
        unsigned l[2],t[2],w[2],h[2];
        for(unsigned style=0;style<2U;style++) {
            unsigned font=style ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE:GARDEN_ASSET_FONT_LARGE_WHITE_BASE;
            unsigned glyph=(unsigned)garden_glyph((char)('0'+digit))-GARDEN_ASSET_FONT_WHITE_BASE;
            atlas_ink(font+glyph,&l[style],&t[style],&w[style],&h[style]);
        }
        CHECK(l[0]==l[1] && t[0]==t[1] && w[0]==w[1] && h[0]==h[1],"all20white/cyan glyphs have identical independently derived ink masks");
    }
    static const unsigned values[]={1U,10U,11U,2U,3U,4U,5U,6U,7U,8U,9U,95U,190U,32767U};
    for(unsigned cpu=0;cpu<2U;cpu++) for(unsigned on=0;on<2U;on++) {
        garden_render_init();garden_render_alpha_support(1);
        if(!on) garden_render_alpha_toggle();
        garden_render_mode(cpu);scope=0U;
        for(sel=0;sel<2U;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"trim suite initializes both histories");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++) for(unsigned b=0;b<2U;b++) {
            sel=b;GardenDraw d={100,100,(uint16_t)(GARDEN_DRAW_SCORE_BASE|values[v]),16};
            memcpy(expected,base,sizeof(expected));oracle_draw(&d,on);
            trace_on=1U;trace_blit_count=trace_fill_count=0U;scope=1U;
            CHECK(garden_render_frame(&d,1U)==GPU_OK,"actual trimmedfont CPU/GPU draw succeeds");scope=0U;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"trimmedfont equals full16x16 source oracle, including real destination Alpha");
            CHECK(garden_render_stats()->sprites==1U,"minus and all digits retain one common history entry");
            char decimal[8];unsigned digits=(unsigned)snprintf(decimal,sizeof(decimal),"%u",values[v]);
            int ink_left=GARDEN_X0+102,ink_top=GARDEN_Y0+107,ink_right=GARDEN_X0+110,ink_bottom=GARDEN_Y0+109;
            unsigned transferred=0U;
            for(unsigned index=0;index<digits;index++) {
                unsigned id=(values[v]==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE)+
                    (unsigned)garden_glyph(decimal[index])-GARDEN_ASSET_FONT_WHITE_BASE;
                const garden_sprite_t *a=garden_asset(id);unsigned l,t,w,h;
                atlas_ink(id,&l,&t,&w,&h);
                int x=GARDEN_X0+112+(int)index*16+(int)l,y=GARDEN_Y0+100+(int)t;
                if(x<ink_left) ink_left=x;
                if(y<ink_top) ink_top=y;
                if(x+(int)w>ink_right) ink_right=x+(int)w;
                if(y+(int)h>ink_bottom) ink_bottom=y+(int)h;
                if(!cpu) {
                    CHECK(trace_blit_count==digits,"trimming never expands the number of digit commands");
                    const BlitTrace *bt=&trace_blits[index];
                    CHECK(bt->src==ASSET_BASE_ADDR+a->offset+t*a->stride+l*2U &&
                          bt->dst==display_back_addr()+(uint32_t)y*DISPLAY_STRIDE_BYTES+(uint32_t)x*2U &&
                          bt->w==w && bt->h==h && bt->ss==32U && bt->ds==DISPLAY_STRIDE_BYTES &&
                          bt->alpha==(on ? 16U:32U),"actual command trims only Key margins, preserves source stride and exact output coordinates/Alpha");
                    transferred+=bt->w*bt->h;
                }
            }
            if(!cpu) {
                CHECK(transferred<digits*256U,"actual GPU source rectangles transfer fewer pixels without reducing font size");
                if(values[v]==95U) CHECK(transferred==280U,"real -95 digit traffic512->280px,45.3125percent source/destination blend rectangle reduction");
            }
            trace_blit_count=trace_fill_count=0U;scope=1U;
            CHECK(garden_render_frame(NULL,0)==GPU_OK,"clear optimized composite on same physical framebuffer");scope=0U;
            CHECK(!memcmp(base,fb(sel),sizeof(base)),"tightened history erases every actual font/sign pixel with no residual");
            if(!cpu) {
                CHECK(trace_fill_count && trace_fills[0].dst==display_back_addr()+(uint32_t)ink_top*DISPLAY_STRIDE_BYTES+(uint32_t)ink_left*2U &&
                      trace_fills[0].w==(unsigned)(ink_right-ink_left) && trace_fills[0].h==(unsigned)(ink_bottom-ink_top) &&
                      trace_fills[0].color==0U,"one erase command is exactly the minus/all-digit ink union, not shrunken text or one rect per digit");
                if(values[v]==95U) CHECK(trace_fills[0].w*trace_fills[0].h==532U,"real -95 history704->532px,24.4318percent fill reduction");
            }
            trace_on=0U;
        }
    }
    puts("PASS: all20actual font ink masks; trimmed CPU/GPU digits equal full-font oracle; -95 GPU blend512->280px, history704->532px; spacing/font size unchanged");
}
static void trimmed_edges_and_overlap(void)
{
    for(unsigned cpu=0;cpu<2U;cpu++) for(unsigned on=0;on<2U;on++) {
        garden_render_init();garden_render_alpha_support(1);if(!on) garden_render_alpha_toggle();
        garden_render_mode(cpu);scope=0U;
        for(sel=0;sel<2U;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"trim stress initializes both FBs");
        memcpy(base,fb(0),sizeof(base));
        for(unsigned tick=0;tick<105U;tick++) for(unsigned b=0;b<2U;b++) {
            sel=(tick+b)&1U;GardenDraw d[35];unsigned n=0;
            static const int xs[]={-92,-44,-38,-12,-4,0,600,630,639};
            static const int ys[]={-16,-14,-8,0,466,478,480};
            if(tick<63U) {
                d[n++]=(GardenDraw){(int16_t)xs[tick%9U],(int16_t)ys[tick%7U],0xFFFFU,16};
                d[n++]=(GardenDraw){(int16_t)(xs[tick%9U]+14),(int16_t)(ys[tick%7U]+14),0x805FU,24};
            } else if(tick<103U) {
                d[n++]=(GardenDraw){320,200,GARDEN_ASSET_BOSS1,32};
                d[n++]=(GardenDraw){300,350,0x2010U,32};
                d[n++]=(GardenDraw){256,130,0x4140U,32};
                static const unsigned value[5]={1U,95U,48U,205U,32767U};
                for(unsigned i=0;i<32U;i++) d[n++]=(GardenDraw){(int16_t)(280+(i%4U)*5U),(int16_t)(170+(i%3U)*2U-(tick-63U)/4U),
                    (uint16_t)(0x8000U|value[i%5U]),(uint8_t)(1U+(i+tick)%32U)};
            }
            memcpy(expected,base,sizeof(expected));for(unsigned i=0;i<n;i++) oracle_draw(&d[i],on);
            scope=1U;CHECK(garden_render_frame(d,n)==GPU_OK,"clipped/32overlapping independent damage composites succeed");scope=0U;
            CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"tight history/croppedfont equals full atlas oracle for all edges, repeated changing widths, moving/fading32-label overlap on alternating FBs");
            CHECK(garden_render_stats()->sprites<=n,"32labels each use at most one dirty-history entry");
        }
        CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,"trim stress has no target/atlas/error faults");
    }
    garden_render_init();sel=0U;scope=0U;garden_render_frame(NULL,0);memcpy(base,fb(0),sizeof(base));
    GardenDraw d={200,200,0xFFFFU,16};fail_at_glyph=2U;
    CHECK(garden_render_frame(&d,1)==GPU_EHW && !fail_at_glyph,"actual second trimmed digit fails after earlier partial painting");
    CHECK(garden_render_stats()->sprites==1U,"failed composite retains exactly one attempted ink-union history");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(base,fb(0),sizeof(base)),"tight ink union also erases a partially failed multi-digit Alpha command sequence");
    puts("PASS: cropped source/target/watchdog guards, all field-edge clipping,32overlapping labels, two-FB dirty erasure and partial multi-digit GPU failure cleanup");
}

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,(ASSET_BASE_ADDR + ASSET_SIZE_BYTES - FB0_ADDR),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"fixed-address host mapping covers exact production FB/atlas pointers");
    FILE *in=fopen(argv[1],"rb");CHECK(in!=NULL,"real atlas opened");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES && fgetc(in)==EOF,"exact unchanged atlas loaded");fclose(in);
    render_sequences();rejections();
    actor_rejections();trimmed_digits();trimmed_edges_and_overlap();
    CHECK(fences>0,"production CPU frame path exercised host fence callback");
    printf("PASS: actor renderer host contract checks=%u\n",checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
