/* SPDX-License-Identifier: GPL-3.0-or-later
 * Neon cockpit layout. Decoration is drawn once per physical framebuffer;
 * periodic HUD updates erase only changed value windows in each buffer.
 */
#include "garden_ui.h"
#include "garden_render.h"
#include <string.h>

static void text(int x, int y, const char *s, unsigned style)
{ garden_render_text_style(x,y,s,style); }

static void panel(int x,int y,int w,int h,uint16_t accent)
{
    garden_render_box(x,y,w,h,GARDEN_UI_PANEL);
    garden_render_box(x,y,w,1,GARDEN_UI_EDGE);
    garden_render_box(x,y+h-1,w,1,GARDEN_UI_EDGE);
    garden_render_box(x,y,1,h,GARDEN_UI_EDGE);
    garden_render_box(x+w-1,y,1,h,GARDEN_UI_EDGE);
    /* Reserve warm pink/gold for playfield hazards and impact feedback.
     * Opaque muted fills avoid another Alpha pass over the static cockpit. */
    (void)accent;
    garden_render_box(x,y,40,1,GARDEN_UI_MUTED);
    garden_render_box(x+w-12,y+h-1,12,1,GARDEN_UI_MUTED);
}

static void key(int x,int y,const char *s)
{
    garden_render_box(x,y,72,20,GARDEN_UI_EDGE);
    garden_render_box(x,y+19,72,1,GARDEN_UI_MUTED);
    if (s[0]=='+') {
        /* The atlas has no plus glyph. Draw the Joy-Con button mark. */
        garden_render_box(x+31,y+9,10,2,GARDEN_UI_CYAN);
        garden_render_box(x+35,y+5,2,10,GARDEN_UI_CYAN);
    } else {
        text(x+(72-(int)strlen(s)*8)/2,y+6,s,GARDEN_TEXT_SMALL_CYAN);
    }
}

static char *decimal(char *out,uint32_t value,unsigned digits)
{
    char reversed[10]; unsigned n=0;
    do { reversed[n++]=(char)('0'+value%10U); value/=10U; } while (value);
    while (n<digits && n<sizeof(reversed)) reversed[n++]='0';
    while (n) *out++=reversed[--n];
    *out=0; return out;
}

static void value(int x,int y,int width,uint32_t v,unsigned digits,unsigned style)
{
    char s[12];
    unsigned clear_height=style>=GARDEN_TEXT_LARGE_WHITE ? 16U:8U;
    decimal(s,v,digits);
    unsigned advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16U:8U;
    if(strlen(s)*advance>(unsigned)width && advance==16U){style-=2U;advance=8U;}
    if(strlen(s)*advance>(unsigned)width){memcpy(s,"MAX",4U);}
    garden_render_box(x,y,width,(int)clear_height,GARDEN_UI_PANEL);
    text(x,y,s,style);
}

uint16_t garden_ui_alpha_pixel(unsigned x,unsigned y)
{
    /* Same 96x32 Alpha workload, now a legible holographic label. The mask
     * is baked when the source panel is initialized, not drawn per frame.
     */
    static const uint8_t label[8][7]={
        {14,17,17,31,17,17,17}, {16,16,16,16,16,16,31},
        {30,17,17,30,16,16,16}, {17,17,17,31,17,17,17},
        {14,17,17,31,17,17,17}, {0,0,0,0,0,0,0},
        {31,16,16,30,16,16,16}, {17,17,10,4,10,17,17}
    };
    if (((y==0U || y==31U) && (x<12U || x>=84U)) ||
        ((x==0U || x==95U) && (y<8U || y>=24U))) return GARDEN_UI_CYAN;
    if ((y==6U || y==25U) && x>=8U && x<88U && (x&3U)<2U) return GARDEN_UI_EDGE;
    if (x>=16U && x<80U && y>=12U && y<19U) {
        unsigned cell=(x-16U)/8U, col=(x-16U)%8U;
        if (col>=1U && col<=5U && (label[cell][y-12U]&(1U<<(5U-col)))) return 0xE7FFU;
    }
    return 0x10A8U;
}

void garden_ui_stars(int x,int y,int w,int h)
{
    /* One star per 64-pixel cell. Erases visit only intersecting cells,
     * usually one for a bullet, rather than scanning the entire field.
     * Clip the star itself to the erase rectangle to preserve other sprites.
     */
    if (w<=0 || h<=0) return;
    int first_col=(x-GARDEN_X0)/64, last_col=(x+w-1-GARDEN_X0)/64;
    int first_row=(y-GARDEN_Y0)/64, last_row=(y+h-1-GARDEN_Y0)/64;
    if (first_col<0) first_col=0;
    if (first_row<0) first_row=0;
    if (last_col>9) last_col=9;
    if (last_row>7) last_row=7;
    for (int row=first_row;row<=last_row;row++) for (int col=first_col;col<=last_col;col++) {
        uint32_t seed=(uint32_t)(row*10+col+1)*2654435761U;
        int sx=GARDEN_X0+col*64+16+(int)(seed&31U);
        int sy=GARDEN_Y0+row*64+8+(int)((seed>>8)&31U);
        int size=(seed&7U)==0U ? 2 : 1;
        int left=sx>x ? sx : x, top=sy>y ? sy : y;
        int right=sx+size<x+w ? sx+size : x+w;
        int bottom=sy+size<y+h ? sy+size : y+h;
        if (bottom>GARDEN_Y0+GARDEN_HEIGHT) bottom=GARDEN_Y0+GARDEN_HEIGHT;
        if (left<right && top<bottom)
            garden_render_box(left,top,right-left,bottom-top,size==2 ? 0x4B36U : 0x192AU);
    }
}

void garden_ui_static(void)
{
    garden_render_box(0,0,1280,720,GARDEN_UI_BACKGROUND);
    /* Quiet rails give the frame depth without obscuring the bullet field. */
    for (int y=112;y<608;y+=32) {
        garden_render_box(12,y,8,1,GARDEN_UI_EDGE);
        garden_render_box(1260,y,8,1,GARDEN_UI_EDGE);
    }
    panel(32,24,1216,72,GARDEN_UI_CYAN);
    text(56,44,"TI60 / RISC-V",GARDEN_TEXT_SMALL_CYAN);
    text(56,64,"FPGA ARCADE SYSTEM",GARDEN_TEXT_SMALL_WHITE);
    text(536,44,"GARDEN OF COLOURED LIGHTS",GARDEN_TEXT_SMALL_CYAN);
    text(568,72,"DANMAKU / STAGE 01",GARDEN_TEXT_SMALL_WHITE);
    text(1080,44,"1280 X 720",GARDEN_TEXT_SMALL_CYAN);
    text(1048,64,"HARDWARE ACCELERATED",GARDEN_TEXT_SMALL_WHITE);

    /* Logical playfield: 640 x 480. The dirty erase restores sparse stars. */
    garden_render_box(308,108,664,504,GARDEN_UI_EDGE);
    garden_render_box(312,112,656,496,GARDEN_UI_BACKGROUND);
    garden_render_box(318,118,644,484,GARDEN_UI_EDGE);
    garden_render_box(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT,0);
    garden_ui_stars(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT);
    for (int corner=0;corner<2;corner++) {
        int x=312+corner*620;
        garden_render_box(x,112,24,1,GARDEN_UI_MUTED);
        garden_render_box(x,605,24,1,GARDEN_UI_MUTED);
        garden_render_box(corner ? 965 : 312,112,1,24,GARDEN_UI_MUTED);
        garden_render_box(corner ? 965 : 312,584,1,24,GARDEN_UI_MUTED);
    }
    for (int x=344;x<944;x+=24) {
        garden_render_box(x,110,4,1,GARDEN_UI_EDGE);
        garden_render_box(x,609,4,1,GARDEN_UI_EDGE);
    }

    panel(32,120,256,236,GARDEN_UI_MAGENTA);
    text(56,140,"FLIGHT DATA",GARDEN_TEXT_SMALL_CYAN);
    garden_render_box(56,162,208,1,GARDEN_UI_EDGE);
    text(56,202,"TOTAL SCORE",GARDEN_TEXT_SMALL_CYAN);
    text(56,266,"HULL / LIVES",GARDEN_TEXT_SMALL_CYAN);
    text(56,314,"BULLETS",GARDEN_TEXT_SMALL_CYAN);
    text(168,314,"PEAK",GARDEN_TEXT_SMALL_CYAN);

    /* Compact truthful measurements; logic/render times remain in UART.
     * Keep actual FPS, both benchmark values, speedup and fault count. */
    panel(32,372,256,176,GARDEN_UI_MUTED);
    text(56,390,"PERFORMANCE",GARDEN_TEXT_SMALL_CYAN);
    text(56,432,"FRAME RATE",GARDEN_TEXT_SMALL_CYAN);
    text(56,450,"GPU BENCH US",GARDEN_TEXT_SMALL_CYAN);
    text(56,468,"CPU BENCH US",GARDEN_TEXT_SMALL_CYAN);
    text(56,486,"SPEEDUP",GARDEN_TEXT_SMALL_CYAN);
    text(56,516,"ERRORS",GARDEN_TEXT_SMALL_CYAN);

    panel(992,120,256,256,GARDEN_UI_CYAN);
    text(1016,140,"JOY-CON / INPUT",GARDEN_TEXT_SMALL_CYAN);
    text(1016,168,"LEFT/RIGHT STICK",GARDEN_TEXT_SMALL_WHITE);
    text(1192,168,"MOVE",GARDEN_TEXT_SMALL_CYAN);
    garden_render_box(1016,184,208,1,GARDEN_UI_EDGE);
    /* Printed right-half face key / matching left-half direction key. */
    key(1016,200,"A/RIGHT"); text(1104,206,"THROW BOMB",GARDEN_TEXT_SMALL_WHITE);
    key(1016,223,"B/DOWN"); text(1104,229,"3 BOSS TEST",GARDEN_TEXT_SMALL_WHITE);
    key(1016,246,"L/R"); text(1104,252,"HOLD FOCUS",GARDEN_TEXT_SMALL_WHITE);
    key(1016,269,"+"); text(1104,275,"PAUSE/RESUME",GARDEN_TEXT_SMALL_WHITE);
    key(1016,292,"-"); text(1104,298,"RESTART",GARDEN_TEXT_SMALL_WHITE);
    key(1016,315,"X/UP"); text(1104,321,"T TEST",GARDEN_TEXT_SMALL_WHITE);
    key(1016,338,"Y/LEFT"); text(1104,344,"CPU/GPU",GARDEN_TEXT_SMALL_WHITE);
    text(1016,364,"AUTO FIRE / 10S IDLE AUTO",GARDEN_TEXT_SMALL_CYAN);

    panel(992,392,256,136,GARDEN_UI_MAGENTA);
    text(1016,410,"TARGET DATA",GARDEN_TEXT_SMALL_CYAN);
    text(1016,444,"HP / ABOVE EACH TARGET",GARDEN_TEXT_SMALL_CYAN);
    text(1016,466,"-DAMAGE / ON IMPACT",GARDEN_TEXT_SMALL_WHITE);
    text(1016,488,"CHARGE. BURST. DODGE.",GARDEN_TEXT_SMALL_CYAN);
    panel(992,544,256,56,GARDEN_UI_CYAN);
    text(1016,562,"FOCUS / PRECISE DODGE",GARDEN_TEXT_SMALL_CYAN);
    text(1016,578,"HOLD L/R TO SHOW HITBOX",GARDEN_TEXT_SMALL_WHITE);

    panel(32,624,256,64,GARDEN_UI_CYAN);
    panel(320,624,640,64,GARDEN_UI_MAGENTA);
    text(344,650,"STAGE 01",GARDEN_TEXT_SMALL_CYAN);
    garden_render_box(488,640,1,32,GARDEN_UI_EDGE);
    text(512,644,"DODGE THE COLOURED LIGHTS",GARDEN_TEXT_SMALL_WHITE);
    text(512,664,"FOCUS. SURVIVE. BREAK THE PATTERN.",GARDEN_TEXT_SMALL_CYAN);
    panel(992,624,256,64,GARDEN_UI_GOLD);
    text(1016,644,"GARDEN 1.0.9 / TI60 PORT",GARDEN_TEXT_SMALL_WHITE);
    text(1016,664,"LINLEY HENZELL / GPL V3",GARDEN_TEXT_SMALL_CYAN);
}

enum {
    HUD_STATE=1U<<0, HUD_SCORE=1U<<1, HUD_LIVES=1U<<2,
    HUD_BULLETS=1U<<3, HUD_PEAK=1U<<4, HUD_BACKEND=1U<<5,
    HUD_FPS=1U<<6, HUD_RENDER=1U<<7, HUD_LOGIC=1U<<8,
    HUD_GPU_BENCH=1U<<9, HUD_CPU_BENCH=1U<<10, HUD_SPEEDUP=1U<<11,
    HUD_ERRORS=1U<<12, HUD_ALPHA=1U<<13, HUD_BOSS_STATUS=1U<<14,
    HUD_BOSS_BAR=1U<<15, HUD_BOSS_PATTERN=1U<<16,
    HUD_ALL=(1U<<17)-1U
};
typedef struct {
    GardenHud last;
    char state[64];
    unsigned valid;
} HudCache;
static HudCache hud_cache[2];
typedef struct { uint32_t score; unsigned valid; } ScoreCache;
static ScoreCache score_cache[2];
static void score_remember(const GardenHud *h,unsigned buffer_index)
{
    score_cache[buffer_index].score=h->score;
    score_cache[buffer_index].valid=1U;
}

/* Health bars are now part of the moving actor draw list. */
static void hud_draw(const GardenHud *h,unsigned changed)
{
    char s[32]; char *out;
    if (changed&HUD_STATE) {
        garden_render_box(56,176,208,8,GARDEN_UI_PANEL);
        text(56,176,h->state ? h->state : "",GARDEN_TEXT_SMALL_CYAN);
    }
    if (changed&HUD_SCORE) value(56,220,208,h->score,8,GARDEN_TEXT_LARGE_WHITE);
    if (changed&HUD_LIVES) {
        value(232,262,32,h->lives,1,GARDEN_TEXT_LARGE_CYAN);
        for (unsigned i=0;i<5U;i++)
            garden_render_box(56+(int)i*42,286,36,6,i<h->lives ? GARDEN_UI_CYAN : GARDEN_UI_EDGE);
    }
    if (changed&HUD_BULLETS) value(56,332,80,h->bullets,3,GARDEN_TEXT_SMALL_WHITE);
    if (changed&HUD_PEAK) value(168,332,96,h->peak,3,GARDEN_TEXT_SMALL_WHITE);

    if (changed&HUD_BACKEND) {
        garden_render_box(56,412,208,8,GARDEN_UI_PANEL);
        text(56,412,h->cpu ? (h->demo ? "CPU / AUTO DEMO" : "CPU / MANUAL FLIGHT") :
                            (h->demo ? "FPGA GPU / AUTO DEMO" : "FPGA GPU / MANUAL"),GARDEN_TEXT_SMALL_CYAN);
    }
    if (changed&HUD_FPS) {
        garden_render_box(176,432,88,8,GARDEN_UI_PANEL);
        out=decimal(s,h->fps10/10U,1); *out++='.'; decimal(out,h->fps10%10U,1);
        text(176,432,s,GARDEN_TEXT_SMALL_WHITE);
    }
    /* Omit the distracting per-frame10cell light gauge. Render/logic times
     * stay cached and continue in the serial report, not extra cockpit rows. */
    if (changed&HUD_GPU_BENCH) value(176,450,88,h->gpu_bench_us,1,GARDEN_TEXT_SMALL_WHITE);
    if (changed&HUD_CPU_BENCH) value(176,468,88,h->cpu_bench_us,1,GARDEN_TEXT_SMALL_WHITE);
    if (changed&HUD_SPEEDUP) {
        garden_render_box(176,486,88,8,GARDEN_UI_PANEL);
        if (h->gpu_bench_us) {
            uint64_t ratio=(uint64_t)h->cpu_bench_us*100U/h->gpu_bench_us;
            if (ratio>9999999U) text(176,486,"OVER 99999X",GARDEN_TEXT_SMALL_WHITE);
            else {
                out=decimal(s,(uint32_t)(ratio/100U),1); *out++='.';
                out=decimal(out,(uint32_t)(ratio%100U),2); *out++='X'; *out=0;
                text(176,486,s,GARDEN_TEXT_SMALL_WHITE);
            }
        } else text(176,486,"N/A",GARDEN_TEXT_SMALL_WHITE);
    }
    if (changed&HUD_ERRORS) value(176,516,88,h->errors,1,GARDEN_TEXT_SMALL_WHITE);

    if (changed&HUD_ALPHA) {
        garden_render_box(56,644,208,28,GARDEN_UI_PANEL);
        text(56,644,!h->alpha_supported ? "ALPHA / UNAVAILABLE" :
                    (h->alpha_enabled ? "ALPHA / ONLINE" : "ALPHA / STANDBY"),GARDEN_TEXT_SMALL_CYAN);
        text(56,664,"HARDWARE / FX BLEND",GARDEN_TEXT_SMALL_WHITE);
        garden_render_box(236,662,28,4,h->alpha_supported && h->alpha_enabled ? GARDEN_UI_CYAN : GARDEN_UI_EDGE);
    }


}

void garden_ui_hud(const GardenHud *h)
{
    if (h) hud_draw(h,HUD_ALL);
}

void garden_ui_hud_reset(void)
{
    hud_cache[0].valid=hud_cache[1].valid=0U;
    score_cache[0].valid=score_cache[1].valid=0U;
}

void garden_ui_score_cached(const GardenHud *h,unsigned buffer_index)
{
    if (!h) return;
    if (buffer_index>=2U) {
        garden_ui_hud_reset(); hud_draw(h,HUD_SCORE); return;
    }
    if (!score_cache[buffer_index].valid || score_cache[buffer_index].score!=h->score)
        hud_draw(h,HUD_SCORE);
    score_remember(h,buffer_index);
}

void garden_ui_boss_cached(const GardenHud *h,unsigned buffer_index)
{
    /* Compatibility no-op: no remote HP/pattern panel is rendered. */
    (void)h; (void)buffer_index;
}

void garden_ui_hud_cached(const GardenHud *h,unsigned buffer_index)
{
    if (!h) return;
    if (buffer_index>=2U) {
        /* The physical destination is unknown, so invalidate both histories. */
        garden_ui_hud_reset(); garden_ui_hud(h); return;
    }
    HudCache *cache=&hud_cache[buffer_index];
    const char *state=h->state ? h->state : "";
    unsigned length=0;
    while (length<sizeof(cache->state) && state[length]) length++;
    if (length==sizeof(cache->state)) {
        /* Preserve the full API's text rather than caching a truncated name. */
        cache->valid=0U; garden_ui_hud(h); score_remember(h,buffer_index); return;
    }
    const GardenHud *old=&cache->last;
    unsigned changed=HUD_ALL;
    if (cache->valid) {
        changed=0U;
        if (strcmp(state,cache->state)) changed|=HUD_STATE;
        if (!score_cache[buffer_index].valid || h->score!=score_cache[buffer_index].score) changed|=HUD_SCORE;
        if (h->lives!=old->lives) changed|=HUD_LIVES;
        if (h->bullets!=old->bullets) changed|=HUD_BULLETS;
        if (h->peak!=old->peak) changed|=HUD_PEAK;
        if (h->cpu!=old->cpu || h->demo!=old->demo) changed|=HUD_BACKEND;
        if (h->fps10!=old->fps10) changed|=HUD_FPS;
        if (h->render_us!=old->render_us) changed|=HUD_RENDER;
        if (h->logic_us!=old->logic_us) changed|=HUD_LOGIC;
        if (h->gpu_bench_us!=old->gpu_bench_us) changed|=HUD_GPU_BENCH|HUD_SPEEDUP;
        if (h->cpu_bench_us!=old->cpu_bench_us) changed|=HUD_CPU_BENCH|HUD_SPEEDUP;
        if (h->errors!=old->errors) changed|=HUD_ERRORS;
        if (h->alpha_supported!=old->alpha_supported || h->alpha_enabled!=old->alpha_enabled ||
            h->alpha_level!=old->alpha_level) changed|=HUD_ALPHA;
    }
    hud_draw(h,changed);
   
    score_remember(h,buffer_index);
    cache->last=*h;
    memcpy(cache->state,state,length+1U);
    cache->last.state=cache->state;
    cache->valid=1U;
}
