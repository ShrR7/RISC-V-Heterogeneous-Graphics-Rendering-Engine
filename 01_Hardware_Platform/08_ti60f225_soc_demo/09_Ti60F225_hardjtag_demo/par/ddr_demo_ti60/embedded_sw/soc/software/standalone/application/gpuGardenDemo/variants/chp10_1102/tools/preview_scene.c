/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual-core scene preview in host RAM; never accesses serial, JTAG, or board MMIO.
 * Scenes are fixed-seed snapshots without fabricated attacks or bullets;
 * every image is host rendering, not a board capture. Timings are sample data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_ui.h"
#include "garden_core.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_fx.h"
#include "garden_stage.h"
#include "garden_feedback.h"
#include "garden_scene.h"
#include "gpu_alpha_ref.h"

#define WIDTH 1280
#define HEIGHT 720
static uint16_t pixels[HEIGHT][WIDTH], reference[HEIGHT][WIDTH];
static uint16_t cached_pixels[2][HEIGHT][WIDTH];
static unsigned char atlas[GARDEN_ATLAS_BYTES];
static unsigned hud_phase, ui_commands, draw_calls;
static GardenRenderStats host_render_stats;

const GardenRenderStats *garden_render_stats(void)
{
    return &host_render_stats;
}

static void bounds(int x,int y,int w,int h)
{
    if (x<0 || y<0 || w<0 || h<0 || x+w>WIDTH || y+h>HEIGHT) {
        fprintf(stderr,"UI bounds failure: %d,%d %dx%d\n",x,y,w,h); exit(2);
    }
    if (hud_phase && x<GARDEN_X0+GARDEN_WIDTH && x+w>GARDEN_X0 &&
        y<GARDEN_Y0+GARDEN_HEIGHT && y+h>GARDEN_Y0) {
        fprintf(stderr,"HUD overwrote the game field\n"); exit(2);
    }
}

void garden_render_box(int x,int y,int w,int h,uint16_t colour)
{
    draw_calls++;
    host_render_stats.commands++;
    bounds(x,y,w,h);
    for (int row=0;row<h;row++) for (int col=0;col<w;col++) pixels[y+row][x+col]=colour;
    ui_commands+=(unsigned)((h+15)/16);
}

static void sprite(unsigned id,int x,int y,unsigned alpha,int in_field)
{
    const garden_sprite_t *s=garden_asset(id);
    if (!s || alpha>32U) { fprintf(stderr,"Invalid asset/alpha %u/%u\n",id,alpha); exit(2); }
    x-=s->anchor_x; y-=s->anchor_y;
    if (!in_field) bounds(x,y,s->width,s->height);
    for (unsigned row=0;row<s->height;row++) for (unsigned col=0;col<s->width;col++) {
        int xx=x+(int)col, yy=y+(int)row;
        if (in_field && (xx<GARDEN_X0 || xx>=GARDEN_X0+GARDEN_WIDTH ||
                        yy<GARDEN_Y0 || yy>=GARDEN_Y0+GARDEN_HEIGHT)) continue;
        unsigned offset=s->offset+row*s->stride+col*2U;
        if (offset+1U>=sizeof(atlas)) { fprintf(stderr,"Atlas overflow\n"); exit(2); }
        uint16_t v=(uint16_t)(atlas[offset]|(unsigned)atlas[offset+1U]<<8);
        if (v!=GARDEN_KEY_COLOR) pixels[yy][xx]=gpu_alpha_rgb565(v,pixels[yy][xx],alpha);
    }
    ui_commands+=(s->height+15U)/16U;
    host_render_stats.commands+=(s->height+15U)/16U;
}

void garden_render_text_style(int x,int y,const char *s,unsigned style)
{
    draw_calls++;
    const int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16 : 8;
    const int start=x;
    const unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE :
                        style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE :
                        style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE : GARDEN_ASSET_FONT_WHITE_BASE;
    while (*s) {
        int id=garden_glyph(*s);
        if (*s=='\n') { x=start; y+=advance+8; }
        else {
            if (id<0) { fprintf(stderr,"Unsupported UI character: %c\n",*s); exit(2); }
            if (*s!=' ') sprite(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x,y,32U,0);
            x+=advance;
        }
        s++;
    }
}

static GardenHud data(const GardenGame *g)
{
    GardenHud h={0};
    h.state=g->mode==GARDEN_MODE_VICTORY ? "VICTORY / R TO PLAY" :
            g->mode==GARDEN_MODE_GAMEOVER ? "GAME OVER / R TO PLAY" :
            g->boss.active ? "STAGE 1 BOSS" : "STAGE 1 WAVES";
    h.score=g->score; h.lives=g->lives;
    h.bullets=g->enemy_bullet_count; h.peak=g->enemy_bullet_peak;
    h.fps10=593; h.render_us=4200; h.logic_us=180;
    h.gpu_bench_us=13400; h.cpu_bench_us=48900;
    h.boss_hp=(uint32_t)(g->boss.hp>0 ? g->boss.hp : 0);
    h.boss_max_hp=(uint32_t)(g->boss.max_hp>0 ? g->boss.max_hp : 0);
    h.boss_attack=g->boss.attack; h.boss_active=g->boss.active;
    h.demo=1; h.alpha_supported=1; h.alpha_enabled=1; h.alpha_level=16;
    return h;
}


typedef struct { int16_t x,y; uint16_t w,h; } HostRect;
static HostRect history[2][GARDEN_DRAW_MAX];
static unsigned history_count[2],history_ready[2];
static uint64_t compared_pixels;

static int clipped_rect(const GardenDraw *draw,HostRect *result)
{
    const garden_sprite_t *s=garden_asset(draw->asset);
    if(!s) {fprintf(stderr,"Invalid scene asset\n");exit(2);}
    int x=GARDEN_X0+draw->x-s->anchor_x;
    int y=GARDEN_Y0+draw->y-s->anchor_y;
    int w=s->width,h=s->height;
    if(x<GARDEN_X0) {w-=GARDEN_X0-x;x=GARDEN_X0;}
    if(y<GARDEN_Y0) {h-=GARDEN_Y0-y;y=GARDEN_Y0;}
    if(x+w>GARDEN_X0+GARDEN_WIDTH) w=GARDEN_X0+GARDEN_WIDTH-x;
    if(y+h>GARDEN_Y0+GARDEN_HEIGHT) h=GARDEN_Y0+GARDEN_HEIGHT-y;
    if(w<=0 || h<=0) return 0;
    *result=(HostRect){(int16_t)x,(int16_t)y,(uint16_t)w,(uint16_t)h};
    return 1;
}

static void paint_draws(const GardenDraw *draws,unsigned count)
{
    if(count>GARDEN_DRAW_MAX) {fprintf(stderr,"Scene draw budget exceeded\n");exit(2);}
    for(unsigned i=0;i<count;i++)
        sprite(draws[i].asset,GARDEN_X0+draws[i].x,GARDEN_Y0+draws[i].y,draws[i].alpha,1);
}

static void check_history_step(const GardenDraw *draws,unsigned count,unsigned sel)
{
    if(!history_ready[sel]) {
        garden_ui_static();
        memcpy(cached_pixels[sel],pixels,sizeof(pixels));
        history_ready[sel]=1U;
    }
    memcpy(pixels,cached_pixels[sel],sizeof(pixels));
    for(unsigned i=0;i<history_count[sel];i++) {
        const HostRect *r=&history[sel][i];
        garden_render_box(r->x,r->y,r->w,r->h,0);
        garden_ui_stars(r->x,r->y,r->w,r->h);
    }
    /* All erases complete before any current sprite is repainted. Every
     * current layer is drawn, including stationary/paused alpha sprites.
     */
    paint_draws(draws,count);
    unsigned n=0U;
    for(unsigned i=0;i<count;i++) if(clipped_rect(&draws[i],&history[sel][n])) n++;
    history_count[sel]=n;
    memcpy(cached_pixels[sel],pixels,sizeof(pixels));
    garden_ui_static();paint_draws(draws,count);
    memcpy(reference,pixels,sizeof(pixels));
    if(memcmp(reference,cached_pixels[sel],sizeof(pixels))) {
        for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++)
            if(reference[y][x]!=cached_pixels[sel][y][x]) {
                fprintf(stderr,"Two-FB erase oracle mismatch buffer=%u x=%d y=%d full=%04X incremental=%04X\n",
                    sel,x,y,reference[y][x],cached_pixels[sel][y][x]);exit(2);
            }
    }
    compared_pixels+=(uint64_t)WIDTH*HEIGHT;
}

static void save(const char *path)
{
    FILE *out=fopen(path,"wb");
    if(!out) {perror(path);exit(2);}
    fprintf(out,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++) {
        unsigned v=pixels[y][x],r=(v>>11)&31U,g=(v>>5)&63U,b=v&31U;
        unsigned char rgb[3]={(unsigned char)((r<<3)|(r>>2)),(unsigned char)((g<<2)|(g>>4)),(unsigned char)((b<<3)|(b>>2))};
        if(fwrite(rgb,1,3,out)!=3) {perror("write");exit(2);}
    }
    if(fclose(out)) {perror("close");exit(2);}
}

typedef struct {
    GardenGame game;
    GardenFx fx;
    GardenStage stage;
    GardenFeedback feedback;
    GardenScene scene;
} Snapshot;

static void observe(Snapshot *s)
{
    garden_fx_observe(&s->fx,&s->game);
    garden_stage_observe(&s->stage,&s->game);
    garden_feedback_observe(&s->feedback,&s->game);
    garden_scene_observe(&s->scene,&s->game);
}

static unsigned make_draws(Snapshot *s,GardenDraw *draws)
{
    unsigned count=garden_scene_build(&s->scene,&s->feedback,&s->fx,&s->game,
        draws,GARDEN_DRAW_MAX,1U,0U);
    unsigned alpha=garden_stage_boss_alpha(&s->stage,&s->game,1U);
    /* The firmware's shared Boss fade must affect shell, core and cue too. */
    for(unsigned i=0;i<count;i++) if(draws[i].asset==GARDEN_ASSET_BOSS1 ||
        draws[i].asset==GARDEN_ASSET_FLASH_BOSS1 || draws[i].asset==GARDEN_ASSET_BOSS_WING_L ||
        draws[i].asset==GARDEN_ASSET_BOSS_WING_R || draws[i].asset==GARDEN_ASSET_BOSS_CORE ||
        draws[i].asset==GARDEN_ASSET_BOSS_CHARGE)
        draws[i].alpha=(uint8_t)(draws[i].alpha<alpha ? draws[i].alpha : alpha);
    if(s->scene.stats.essential_dropped || s->feedback.stats.essential_dropped)
        {fprintf(stderr,"Essential draw lost in real-core preview\n");exit(2);}
    return count;
}

static unsigned visible_bullets(const GardenGame *g)
{
    unsigned n=0U;
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) {
        const GardenEnemyBullet *b=&g->enemy_bullets[i];
        n+=b->active && b->x>=0 && b->x<GARDEN_WIDTH*GARDEN_GRAIN &&
           b->y>=0 && b->y<GARDEN_HEIGHT*GARDEN_GRAIN;
    }
    return n;
}

static void output_snapshot(const char *prefix,const char *name,Snapshot *s)
{
    GardenDraw draws[GARDEN_DRAW_MAX];
    garden_ui_hud_reset();garden_ui_static();
    GardenHud h=data(&s->game);
    hud_phase=1;garden_ui_hud(&h);hud_phase=0;
    unsigned count=make_draws(s,draws);
    paint_draws(draws,count);
    garden_stage_ui_reset(&s->stage);
    hud_phase=1;
    if(garden_stage_draw(&s->stage,&s->game,0U)<0)
        {fprintf(stderr,"Host stage draw failed\n");exit(2);}
    hud_phase=0;
    uint64_t clipped_pixels=0U;
    unsigned bands=0U;
    for(unsigned i=0;i<count;i++) {
        HostRect r;
        if(clipped_rect(&draws[i],&r)) {
            clipped_pixels+=(uint64_t)r.w*r.h;
            bands+=(r.h+15U)/16U;
        }
    }
    char path[1024];int n=snprintf(path,sizeof(path),"%s_%s.ppm",prefix,name);
    if(n<0 || (unsigned)n>=sizeof(path)) exit(2);
    save(path);
    printf("{\"name\":\"%s\",\"seed\":12345,\"tick\":%u,\"mode\":%u,\"score\":%u,\"boss_flash\":%u,\"boss_hp\":%d,\"boss_attack\":%u,\"recycle\":%u,\"move_ticks\":%u,\"visible_bullets\":%u,\"draws\":%u,\"wings\":%u,\"core\":%u,\"charge\":%u,\"charge_countdown\":%u,\"burst_active\":%u,\"backgrounds\":%u,\"essential_dropped\":%u,\"decorative_dropped\":%u,\"sprite_band_estimate\":%u,\"clipped_sprite_pixels\":%llu}\n",
        name,s->game.tick,s->game.mode,s->game.score,s->game.boss.flash,s->game.boss.hp,
        s->game.boss.attack,s->game.boss.recycle,s->game.boss.move_ticks,visible_bullets(&s->game),count,
        s->scene.stats.wings,s->scene.stats.core,s->scene.stats.charge,s->scene.stats.charge_countdown,
        s->scene.stats.burst_active,s->scene.stats.backgrounds,s->scene.stats.essential_dropped,
        s->scene.stats.decorative_dropped,bands,(unsigned long long)clipped_pixels);
}

int main(int argc,char **argv)
{
    if(argc!=3) {fprintf(stderr,"Usage: preview_scene ATLAS OUTPUT_PREFIX\n");return 2;}
    FILE *in=fopen(argv[1],"rb");
    if(!in || fread(atlas,1,sizeof(atlas),in)!=sizeof(atlas) || fgetc(in)!=EOF)
        {fprintf(stderr,"Atlas exact size load failed\n");return 2;}
    fclose(in);
    Snapshot current={0},normal={0},charge={0},animation[5];
    unsigned normal_found=0U,charge_found=0U,animation_found=0U,best=0U;
    uint32_t animation_start=UINT32_MAX;
    garden_init(&current.game,12345U);
    garden_fx_reset(&current.fx);garden_stage_reset(&current.stage);
    garden_feedback_reset(&current.feedback);garden_scene_reset(&current.scene);observe(&current);
    unsigned checks=0U;
    for(unsigned step=0U;step<8000U;step++) {
        garden_step(&current.game,0U);observe(&current);
        unsigned visible=visible_bullets(&current.game);
        if(current.game.mode==GARDEN_MODE_BOSS && current.game.boss.active &&
           !current.game.boss.flash && !current.scene.charge_countdown && !current.scene.burst_ticks &&
           garden_stage_boss_alpha(&current.stage,&current.game,1U)==32U &&
           (!normal_found || visible>best)) {
            normal=current;normal_found=1U;best=visible;
        }
        if(!charge_found && current.scene.charge_countdown==3U &&
           !current.game.boss.move_ticks && garden_stage_boss_alpha(&current.stage,&current.game,1U)==32U) {
            charge=current;charge_found=1U;
        }
        if(animation_start==UINT32_MAX && current.scene.charge_countdown==12U &&
           !current.game.boss.move_ticks && garden_stage_boss_alpha(&current.stage,&current.game,1U)==32U) {
            animation_start=current.game.tick;
        }
        if(animation_start!=UINT32_MAX && animation_found<5U &&
           current.game.tick==animation_start+animation_found*3U)
            animation[animation_found++]=current;
        /* Alternating real tick snapshots include entry, attack, UI footer
         * epochs and edges. These are RAM renderer-history checks, not MMIO.
         */
        if(step%8U==0U) {
            GardenDraw draws[GARDEN_DRAW_MAX];unsigned count=make_draws(&current,draws);
            check_history_step(draws,count,checks&1U);checks++;
        }
        if(current.game.mode==GARDEN_MODE_VICTORY || current.game.mode==GARDEN_MODE_GAMEOVER) break;
    }
    if(!normal_found || !charge_found || animation_found!=5U)
        {fprintf(stderr,"Missing actual-core normal/charge/animation snapshots %u/%u/%u\n",normal_found,charge_found,animation_found);return 2;}
    unsigned prior_shots=0U,release_shots=0U;
    for(unsigned i=0;i<3U;i++) {
        prior_shots+=animation[0].game.boss_attack_shots[i];
        release_shots+=animation[4].game.boss_attack_shots[i];
    }
    if(release_shots!=prior_shots+1U || !animation[4].scene.burst_ticks ||
       animation[4].scene.charge_countdown || normal.game.boss.flash ||
       charge.scene.charge_countdown>12U || !charge.scene.charge_countdown)
        {fprintf(stderr,"Preview phase precondition failed\n");return 2;}
    Snapshot paused=normal;
    garden_step(&paused.game,GARDEN_INPUT_PAUSE);observe(&paused);
    GardenDraw frozen[GARDEN_DRAW_MAX],again[GARDEN_DRAW_MAX];
    unsigned frozen_count=make_draws(&paused,frozen);
    for(unsigned i=0U;i<12U;i++) {
        garden_step(&paused.game,0U);observe(&paused);
        unsigned n=make_draws(&paused,again);
        if(n!=frozen_count) {fprintf(stderr,"Paused draw count changed\n");return 2;}
        for(unsigned j=0U;j<n;j++) if(frozen[j].asset!=again[j].asset || frozen[j].alpha!=again[j].alpha ||
            frozen[j].x!=again[j].x || frozen[j].y!=again[j].y)
            {fprintf(stderr,"Paused draw list changed\n");return 2;}
        check_history_step(again,n,(checks++)&1U);
    }
    printf("PASS: %u alternate physical-FB dirty-history RAM oracle frames, %llu exact RGB565 pixel comparisons; 12 paused identical draw-list frames; no hardware access\n",
        checks,(unsigned long long)compared_pixels);
    output_snapshot(argv[2],"boss",&normal);
    output_snapshot(argv[2],"charge",&charge);
    output_snapshot(argv[2],"parallax_a",&animation[0]);
    output_snapshot(argv[2],"parallax_b",&animation[4]);
    for(unsigned i=0;i<5U;i++) {
        char name[24];int n=snprintf(name,sizeof(name),"charge_%u",i);
        if(n<0 || (unsigned)n>=sizeof(name)) return 2;
        output_snapshot(argv[2],name,&animation[i]);
    }
    return 0;
}
