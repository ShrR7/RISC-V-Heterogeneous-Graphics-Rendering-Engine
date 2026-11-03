/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host RAM renderer for the production UI layout and atlas, never board MMIO.
 * Scenes are fixed-seed game snapshots plus an illustrative FX snapshot;
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
    if (!s) { fprintf(stderr,"Invalid asset %u\n",id); exit(2); }
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

static void check_updates(void)
{
    GardenGame g; garden_init(&g,12345U);
    GardenHud high=data(&g), low=data(&g);
    high.score=4294967295U; high.lives=5; high.bullets=192; high.peak=192;
    high.fps10=9999U; low.fps10=0;
    high.render_us=4294967295U; high.cpu_bench_us=4294967295U;
    high.gpu_bench_us=1;
    high.errors=4294967295U; high.boss_active=1; high.boss_hp=100; high.boss_max_hp=100;
    low.score=1; low.lives=1; low.bullets=0; low.peak=0; low.cpu=1;
    low.alpha_enabled=0; low.alpha_supported=0; low.gpu_bench_us=0;
    garden_ui_static(); hud_phase=1;
    garden_ui_hud(&high); garden_ui_hud(&low);
    memcpy(reference,pixels,sizeof(pixels));
    hud_phase=0; garden_ui_static(); hud_phase=1; garden_ui_hud(&low);
    if (memcmp(reference,pixels,sizeof(pixels))) {
        fprintf(stderr,"HUD update left stale pixels or erased static labels\n"); exit(2);
    }
    hud_phase=0;
    puts("PASS: UI bounds, supported glyphs, playfield isolation, high-to-low HUD redraw");
}

static unsigned check_cached_step(const GardenHud *h,unsigned buffer_index,int redraw)
{
    memcpy(pixels,cached_pixels[buffer_index],sizeof(pixels));
    hud_phase=1; ui_commands=draw_calls=0;
    garden_ui_hud_cached(h,buffer_index);
    unsigned cost=ui_commands;
    if (!!draw_calls!=!!redraw) {
        fprintf(stderr,"Cached HUD redraw expectation failed for buffer %u\n",buffer_index); exit(2);
    }
    memcpy(cached_pixels[buffer_index],pixels,sizeof(pixels));
    ui_commands=draw_calls=0;
    garden_ui_hud_cached(h,buffer_index);
    if (draw_calls || ui_commands || memcmp(cached_pixels[buffer_index],pixels,sizeof(pixels))) {
        fprintf(stderr,"Unchanged cached HUD drew or changed pixels\n"); exit(2);
    }
    /* The oracle uses the production full HUD API and the same RAM renderer,
     * fonts and static layout, not a separately reimplemented HUD.
     */
    hud_phase=0; garden_ui_static(); hud_phase=1; garden_ui_hud(h);
    memcpy(reference,pixels,sizeof(pixels));
    if (memcmp(reference,cached_pixels[buffer_index],sizeof(pixels))) {
        fprintf(stderr,"Cached HUD differs from full redraw for buffer %u\n",buffer_index); exit(2);
    }
    memcpy(pixels,cached_pixels[buffer_index],sizeof(pixels));
    hud_phase=0;
    return cost;
}

static void check_cached_updates(void)
{
    GardenGame g; garden_init(&g,12345U);
    GardenHud base=data(&g);
    char original_state[32]="STAGE 1 WAVES", same_state[32]="STAGE 1 WAVES";
    base.state=original_state;
    garden_ui_hud_reset();
    for (unsigned buffer=0;buffer<2U;buffer++) {
        garden_ui_static(); memcpy(cached_pixels[buffer],pixels,sizeof(pixels));
        check_cached_step(&base,buffer,1);
    }
    /* Independent buffers must both notice same-pointer mutations. Conversely
     * a new pointer with identical text must not trigger any drawing call.
     */
    base.state=same_state;
    check_cached_step(&base,0,0); check_cached_step(&base,1,0);
    strcpy(same_state,"PAUSED");
    check_cached_step(&base,0,1); check_cached_step(&base,1,1);
    strcpy(same_state,"STAGE 1 WAVES");
    check_cached_step(&base,1,1); check_cached_step(&base,0,1);
    for (unsigned change=0;change<21U;change++) {
        GardenHud changed=base;
        switch (change) {
            case 0: changed.score=4294967295U; break;
            case 1: changed.lives=5; break;
            case 2: changed.bullets=192; break;
            case 3: changed.peak=192; break;
            case 4: changed.cpu=1; break;
            case 5: changed.demo=0; break;
            case 6: changed.fps10=9999; break;
            case 7: changed.render_us=4294967295U; break;
            case 8: changed.logic_us=4294967295U; break;
            case 9: changed.gpu_bench_us=1; break;
            case 10: changed.cpu_bench_us=4294967295U; break;
            case 11: changed.errors=4294967295U; break;
            case 12: changed.alpha_supported=0; break;
            case 13: changed.alpha_enabled=0; break;
            case 14: changed.alpha_level=8; break;
            case 15: changed.boss_active=1; break;
            case 16: changed.boss_hp=100; break;
            case 17: changed.boss_max_hp=100; break;
            case 18: changed.boss_attack=3; break;
            case 19: changed.gpu_bench_us=0; break;
            default: changed.state="VICTORY / R TO PLAY"; break;
        }
        unsigned buffer=change&1U;
        check_cached_step(&changed,buffer,1);
        check_cached_step(&base,buffer,1);
    }
    GardenHud high=base, low=base;
    high.score=4294967295U; high.lives=5; high.bullets=192; high.peak=192;
    high.fps10=9999; high.render_us=4294967295U; high.logic_us=4294967295U;
    high.cpu_bench_us=4294967295U; high.gpu_bench_us=1; high.errors=4294967295U;
    high.boss_active=1; high.boss_hp=100; high.boss_max_hp=100; high.boss_attack=3;
    high.state="STAGE 1 BOSS";
    low.score=1; low.lives=1; low.bullets=0; low.peak=0; low.fps10=0;
    low.cpu=1; low.demo=0; low.alpha_enabled=0; low.alpha_supported=0;
    low.gpu_bench_us=0; low.state="GAME OVER / R TO PLAY";
    for (unsigned buffer=0;buffer<2U;buffer++) check_cached_step(&high,buffer,1);
    GardenHud damaged=high; damaged.boss_hp=1;
    unsigned damage_cost=check_cached_step(&damaged,0,1);
    check_cached_step(&damaged,1,1);
    for (unsigned buffer=0;buffer<2U;buffer++) check_cached_step(&low,buffer,1);
    garden_ui_hud_reset();
    for (unsigned buffer=0;buffer<2U;buffer++) check_cached_step(&low,buffer,1);
    /* An invalid index falls back to full drawing and invalidates both cache
     * histories, since this API cannot identify its physical destination.
     */
    hud_phase=1; draw_calls=0; garden_ui_hud_cached(&low,2);
    if (!draw_calls) { fprintf(stderr,"Invalid buffer did not redraw HUD\n"); exit(2); }
    hud_phase=0;
    for (unsigned buffer=0;buffer<2U;buffer++) check_cached_step(&low,buffer,1);
    /* Overlength text must not be truncated into a falsely unchanged cache.
     * Spaces exercise the fallback without overflowing the normal glyph area.
     */
    char long_state[72]; memset(long_state,' ',sizeof(long_state)-1U); long_state[sizeof(long_state)-1U]=0;
    GardenHud long_name=low; long_name.state=long_state;
    hud_phase=1;
    for (unsigned i=0;i<2U;i++) {
        draw_calls=0; garden_ui_hud_cached(&long_name,0);
        if (!draw_calls) { fprintf(stderr,"Overlength state incorrectly cached\n"); exit(2); }
    }
    hud_phase=0; garden_ui_hud_reset();
    printf("PASS: cached HUD matches full redraw across two buffers, 21 field changes, mutable state, high-to-low, reset and invalid buffer; unchanged calls=0; HP-only commands=%u\n",damage_cost);
}

static void check_stars(void)
{
    garden_ui_static(); memcpy(reference,pixels,sizeof(pixels));
    for (unsigned i=0;i<200U;i++) {
        unsigned seed=(i+1U)*2654435761U;
        int x=GARDEN_X0+(int)(seed%640U),y=GARDEN_Y0+(int)((seed>>10)%480U);
        int w=1+(int)((seed>>3)%160U),h=1+(int)((seed>>7)%120U);
        if (x+w>GARDEN_X0+GARDEN_WIDTH) w=GARDEN_X0+GARDEN_WIDTH-x;
        if (y+h>GARDEN_Y0+GARDEN_HEIGHT) h=GARDEN_Y0+GARDEN_HEIGHT-y;
        garden_render_box(x,y,w,h,0); garden_ui_stars(x,y,w,h);
        if (memcmp(reference,pixels,sizeof(pixels))) {
            fprintf(stderr,"Partial star restore changed pixels outside the erase or left a hole\n"); exit(2);
        }
    }
    puts("PASS: 200 partial star restores, including clipped cell and field edges");
}

static void scene(const GardenGame *g,GardenFx *fx,GardenStage *stage,GardenFeedback *feedback,unsigned focus)
{
    GardenDraw draws[GARDEN_DRAW_MAX];
    unsigned n=garden_feedback_build(feedback,fx,g,draws,GARDEN_DRAW_MAX,1,focus);
    unsigned boss_alpha=garden_stage_boss_alpha(stage,g,1);
    for (unsigned i=0;i<n;i++)
        if (draws[i].asset==GARDEN_ASSET_BOSS1 || draws[i].asset==GARDEN_ASSET_FLASH_BOSS1)
            draws[i].alpha=(uint8_t)boss_alpha;
    for (unsigned i=0;i<n;i++) sprite(draws[i].asset,GARDEN_X0+draws[i].x,
        GARDEN_Y0+draws[i].y,draws[i].alpha,1);
    if (stage) {
        hud_phase=1; (void)garden_stage_draw(stage,g,0); hud_phase=0;
    }
}

static void save(const char *path);

static void illustrative_fx(const char *prefix)
{
    GardenGame g; GardenFx fx; GardenFeedback feedback;
    memset(&g,0,sizeof(g));
    g.tick=100; g.mode=GARDEN_MODE_BOSS; g.lives=3; g.score=920; g.demo_mode=1;
    g.player.active=1; g.player.x=210000; g.player.y=360000;
    g.boss=(GardenBoss){.active=1,.flash=3,.x=400000,.y=100000,.hp=1000,.max_hp=1000};
    g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.flash=3,.x=180000,.y=120000,.hp=300};
    g.enemies[1]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_STOPPER,.x=480000,.y=230000};
    g.effects[0]=(GardenEffect){.active=1,.age=3,.big=1,.x=160000,.y=240000};
    g.effects[1]=(GardenEffect){.active=1,.age=7,.big=0,.x=340000,.y=280000};
    for(unsigned i=0;i<28;i++) g.enemy_bullets[i]=(GardenEnemyBullet){
        .active=1,.type=i%2U ? GARDEN_BULLET_DART:GARDEN_BULLET_DIAMOND,
        .colour=(uint8_t)(1U+i%5U),.angle=(uint16_t)((i%8U)*128U),
        .x=(int32_t)((80U+(i%7U)*74U)*GARDEN_GRAIN),
        .y=(int32_t)((170U+(i/7U)*64U)*GARDEN_GRAIN)};
    g.enemy_bullet_count=g.enemy_bullet_peak=28;
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=390000,.y=140000,.vy=-14000,.age=10,.damage=95};
    g.player_bullets[1]=(GardenPlayerBullet){.active=1,.x=180000,.y=138000,.vy=-14000,.age=10,.damage=95};
    garden_fx_reset(&fx); garden_fx_set_flags(&fx,GARDEN_FX_ALL);
    garden_fx_observe(&fx,&g);
    garden_feedback_reset(&feedback); garden_feedback_observe(&feedback,&g);
    g.tick++; g.player.x=235000; g.player.y=350000;
    g.boss.hp-=95; g.enemies[0].hp-=95;
    for(unsigned i=0;i<2;i++) {g.player_bullets[i].active=0;g.player_bullets[i].age++;g.player_bullets[i].y-=14000;}
    garden_fx_observe(&fx,&g); garden_feedback_observe(&feedback,&g);
    g.tick++; g.player.x=260000; g.player.y=340000;
    garden_fx_observe(&fx,&g); garden_feedback_observe(&feedback,&g);
    garden_ui_static(); GardenHud h=data(&g);
    hud_phase=1; garden_ui_hud(&h); hud_phase=0;
    scene(&g,&fx,NULL,&feedback,0);
    const GardenFxStats *s=garden_fx_stats(&fx);
    if(s->flash_sprites!=2 || s->ghosts!=2 || s->rings!=2) {
        fprintf(stderr,"Illustrative scene did not exercise all three production FX\n"); exit(2);
    }
    char path[1024];
    int n=snprintf(path,sizeof(path),"%s_fx.ppm",prefix);
    if(n<0 || (unsigned)n>=sizeof(path)) exit(2);
    save(path);
    n=snprintf(path,sizeof(path),"%s_feedback.ppm",prefix);
    if(n<0 || (unsigned)n>=sizeof(path)) exit(2);
    save(path);
    if(feedback.stats.sparks!=2 || feedback.stats.marker!=1 || feedback.stats.bullet_anchored_hits!=2) {
        fprintf(stderr,"Illustration did not exercise real feedback observer/build\n");exit(2);
    }
    printf("PREVIEW FX/FEEDBACK ILLUSTRATIVE HOST SAMPLE: flash=%u ghosts=%u rings=%u sparks=%u marker=%u corroborated_hits=%u; not a board capture or measured performance\n",
        s->flash_sprites,s->ghosts,s->rings,feedback.stats.sparks,feedback.stats.marker,feedback.stats.bullet_anchored_hits);
}

static void marker_edge_illustration(const char *prefix)
{
    GardenGame g; GardenFx fx; GardenFeedback feedback; GardenDraw draws[GARDEN_DRAW_MAX];
    garden_init(&g,12345U);
    memset(g.enemies,0,sizeof(g.enemies)); memset(g.player_bullets,0,sizeof(g.player_bullets));
    memset(g.enemy_bullets,0,sizeof(g.enemy_bullets)); memset(g.effects,0,sizeof(g.effects));
    memset(&g.boss,0,sizeof(g.boss));
    g.demo_mode=1; g.player.active=1; g.player.respawning=0;
    garden_fx_reset(&fx); garden_fx_set_flags(&fx,0);
    garden_feedback_reset(&feedback); garden_feedback_observe(&feedback,&g);
    garden_ui_static(); GardenHud h=data(&g); hud_phase=1;garden_ui_hud(&h);hud_phase=0;
    const int positions[4][2]={{0,0},{639,0},{0,479},{639,479}};
    for(unsigned i=0;i<4;i++) {
        g.player.x=positions[i][0]*GARDEN_GRAIN;g.player.y=positions[i][1]*GARDEN_GRAIN;
        g.player.grace=(uint16_t)(i&1U);
        unsigned n=garden_feedback_build(&feedback,&fx,&g,draws,GARDEN_DRAW_MAX,1,1);
        if(!feedback.stats.marker || draws[n-1].x!=positions[i][0] || draws[n-1].y!=positions[i][1]) {
            fprintf(stderr,"Field-edge marker moved away from core centre\n");exit(2);
        }
        for(unsigned j=0;j<n;j++) sprite(draws[j].asset,GARDEN_X0+draws[j].x,
            GARDEN_Y0+draws[j].y,draws[j].alpha,1);
    }
    char path[1024];int n=snprintf(path,sizeof(path),"%s_focus_edges.ppm",prefix);
    if(n<0 || (unsigned)n>=sizeof(path)) exit(2);
    save(path);
    puts("PASS: field-edge focus markers clip without centre movement; four-position HOST ILLUSTRATION NOT A REAL GAME FRAME");
}

static void save(const char *path)
{
    FILE *out=fopen(path,"wb");
    if (!out) { perror(path); exit(2); }
    fprintf(out,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    for (int y=0;y<HEIGHT;y++) for (int x=0;x<WIDTH;x++) {
        unsigned v=pixels[y][x],r=(v>>11)&31U,g=(v>>5)&63U,b=v&31U;
        unsigned char rgb[3]={(unsigned char)((r<<3)|(r>>2)),(unsigned char)((g<<2)|(g>>4)),(unsigned char)((b<<3)|(b>>2))};
        if (fwrite(rgb,1,3,out)!=3) { perror("write"); exit(2); }
    }
    fclose(out);
}

int main(int argc,char **argv)
{
    if (argc!=3) { fprintf(stderr,"Usage: preview_ui ATLAS OUTPUT_PREFIX\n"); return 2; }
    FILE *in=fopen(argv[1],"rb");
    if (!in || fread(atlas,1,sizeof(atlas),in)!=sizeof(atlas)) {
        fprintf(stderr,"Atlas load failed\n"); return 2;
    }
    fclose(in); check_updates(); check_cached_updates(); check_stars();
    GardenGame g,snapshots[4]; GardenFx fx,fx_snapshots[4];
    GardenStage stage,stage_snapshots[4];
    GardenFeedback feedback,feedback_snapshots[4];
    unsigned best[2]={0,0}, found[4]={0,0,0,0};
    garden_init(&g,12345U);
    garden_fx_reset(&fx); garden_fx_set_flags(&fx,GARDEN_FX_ALL); garden_fx_observe(&fx,&g);
    garden_stage_reset(&stage); garden_stage_observe(&stage,&g);
    garden_feedback_reset(&feedback);garden_feedback_observe(&feedback,&g);
    for (unsigned i=0;i<8000U;i++) {
        garden_step(&g,0);
        garden_fx_observe(&fx,&g);
        garden_stage_observe(&stage,&g);
        garden_feedback_observe(&feedback,&g);
        if (g.mode==GARDEN_MODE_VICTORY) {
            snapshots[3]=g; fx_snapshots[3]=fx; stage_snapshots[3]=stage; feedback_snapshots[3]=feedback;found[3]=1;
            break;
        }
        if (g.mode==GARDEN_MODE_GAMEOVER) break;
        if (!found[2] && stage.boss_entry_valid && g.tick-stage.boss_entry_tick==40U) {
            snapshots[2]=g; fx_snapshots[2]=fx; stage_snapshots[2]=stage; feedback_snapshots[2]=feedback;found[2]=1;
        }
        unsigned which=g.boss.active ? 1U : 0U, visible=0;
        for (unsigned j=0;j<GARDEN_MAX_ENEMY_BULLETS;j++) {
            const GardenEnemyBullet *p=&g.enemy_bullets[j];
            if (p->active && p->x>=0 && p->x<GARDEN_WIDTH*GARDEN_GRAIN &&
                p->y>=0 && p->y<GARDEN_HEIGHT*GARDEN_GRAIN) visible++;
        }
        if (!found[which] || visible>best[which]) {
            best[which]=visible; snapshots[which]=g; fx_snapshots[which]=fx;
            stage_snapshots[which]=stage;feedback_snapshots[which]=feedback;found[which]=1;
        }
    }
    static const char *const names[4]={"waves","boss","boss_entry","victory"};
    for (unsigned i=0;i<4U;i++) {
        char path[1024];
        if (!found[i]) { fprintf(stderr,"Missing preview game phase %u\n",i); return 2; }
        ui_commands=0; garden_ui_static();
        unsigned static_cost=ui_commands; ui_commands=0;
        GardenHud h=data(&snapshots[i]); hud_phase=1; garden_ui_hud(&h); hud_phase=0;
        unsigned dynamic_cost=ui_commands;
        garden_stage_ui_reset(&stage_snapshots[i]);
        unsigned boss_alpha=garden_stage_boss_alpha(&stage_snapshots[i],&snapshots[i],1);
        scene(&snapshots[i],&fx_snapshots[i],&stage_snapshots[i],&feedback_snapshots[i],0);
        int n=snprintf(path,sizeof(path),"%s_%s.ppm",argv[2],names[i]);
        if (n<0 || (unsigned)n>=sizeof(path)) return 2;
        save(path);
        printf("PREVIEW %s seed=12345 tick=%u mode=%u boss_alpha=%u score=%u visible_bullets=%u static_commands=%u hud_commands=%u; HOST SNAPSHOT NOT BOARD; TIMINGS SAMPLE VALUES\n",
            names[i],snapshots[i].tick,snapshots[i].mode,boss_alpha,snapshots[i].score,
            i<2U ? best[i]:snapshots[i].enemy_bullet_count,static_cost,dynamic_cost);
    }
    illustrative_fx(argv[2]);
    marker_edge_illustration(argv[2]);
    return 0;
}
