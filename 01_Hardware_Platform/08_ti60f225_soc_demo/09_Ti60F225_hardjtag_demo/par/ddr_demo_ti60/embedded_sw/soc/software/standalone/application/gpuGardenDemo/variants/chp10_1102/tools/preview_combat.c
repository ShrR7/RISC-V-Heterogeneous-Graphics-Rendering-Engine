/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual-core combined combat preview through the production renderer in host RAM.
 * Screenshots and animations are host illustrations, never board captures.
 */
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

static unsigned checks,sel,scope,fail_glyph,fail_box,reject_target;
static unsigned calls,glyph_calls,fill_calls,fences;
static uint64_t oracle_pixels;
static uint16_t expected[DISPLAY_HEIGHT][DISPLAY_WIDTH],base[DISPLAY_HEIGHT][DISPLAY_WIDTH];

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
    CHECK(alpha<=32U,"Alpha command validated in 0..32");
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
static void oracle_draw(const GardenDraw *d,unsigned alpha_on)
{
    unsigned alpha=alpha_on ? d->alpha:32U;
    int x=GARDEN_X0+d->x,y=GARDEN_Y0+d->y;
    if(!(d->asset&GARDEN_DRAW_SCORE_BASE)) { ref_sprite(d->asset,x,y,alpha);return; }
    unsigned v=d->asset&0x7FFFU;
    if(!v || !alpha) return;
    char decimal[8]; int n=snprintf(decimal,sizeof(decimal),"%u",v);
    CHECK(n>0 && n<=5,"virtual positive score bounded to five digits");
    unsigned font=v==1U ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE;
    uint16_t color=v==1U ? 0xFFFFU:GARDEN_UI_CYAN;
    if(alpha<32U) color=gpu_alpha_rgb565(color,0U,alpha);
    ref_box(x+2,y+7,8,2,color);ref_box(x+5,y+4,2,8,color);
    for(int i=0;i<n;i++) {
        int id=garden_glyph(decimal[i]);CHECK(id>=0,"real score digit glyph exists");
        ref_sprite(font+(unsigned)id-GARDEN_ASSET_FONT_WHITE_BASE,x+12+i*16,y,alpha);
    }
}


#include "garden_core.h"
#include "garden_fx.h"
#include "garden_feedback.h"
#include "garden_scene.h"
#include "garden_combat.h"
#include "garden_stage.h"

typedef struct {
    GardenGame game;
    GardenFx fx;
    GardenFeedback feedback;
    GardenScene scene;
    GardenCombat combat;
    GardenStage stage;
} Snapshot;

static void observe(Snapshot *s)
{
    garden_fx_observe(&s->fx,&s->game);
    garden_stage_observe(&s->stage,&s->game);
    garden_feedback_observe(&s->feedback,&s->game);
    garden_scene_observe(&s->scene,&s->game);
    garden_combat_observe(&s->combat,&s->game);
}
static unsigned make_draws(Snapshot *s,GardenDraw *out)
{
    unsigned count=garden_combat_build(&s->combat,&s->scene,&s->feedback,&s->fx,&s->game,out,GARDEN_DRAW_MAX,1U,0U);
    unsigned alpha=garden_stage_boss_alpha(&s->stage,&s->game,1U);
    for(unsigned i=0;i<count;i++) {
        unsigned asset=out[i].asset;
        if(asset==GARDEN_ASSET_BOSS1 || asset==GARDEN_ASSET_FLASH_BOSS1) out[i].alpha=(uint8_t)alpha;
        else if(asset==GARDEN_ASSET_BOSS_WING_L || asset==GARDEN_ASSET_BOSS_WING_R ||
                asset==GARDEN_ASSET_BOSS_CORE || asset==GARDEN_ASSET_BOSS_CHARGE)
            if(out[i].alpha>alpha) out[i].alpha=(uint8_t)alpha;
    }
    CHECK(!s->combat.stats.essential_dropped,"actual combined list retains every essential draw");
    if(s->combat.stats.marker) CHECK(count &&
        (out[count-1U].asset==GARDEN_ASSET_FOCUS_VULNERABLE || out[count-1U].asset==GARDEN_ASSET_FOCUS_GRACE),
        "actual player collision marker paints last after all score/impact layers");
    return count;
}
static GardenHud hud(const GardenGame *g)
{
    GardenHud h={0};
    h.state=g->mode==GARDEN_MODE_VICTORY ? "VICTORY / R TO PLAY":
            g->mode==GARDEN_MODE_GAMEOVER ? "GAME OVER / R TO PLAY":
            g->boss.active ? "STAGE 1 BOSS":"STAGE 1 WAVES";
    h.score=g->score;h.lives=g->lives;h.bullets=g->enemy_bullet_count;h.peak=g->enemy_bullet_peak;
    /* Illustrative performance display only, never measurements. */
    h.fps10=601;h.render_us=3200;h.logic_us=200;h.gpu_bench_us=9000;h.cpu_bench_us=39000;
    h.demo=g->demo_mode;h.alpha_supported=1;h.alpha_enabled=1;h.alpha_level=16;
    h.boss_active=g->boss.active;h.boss_hp=(uint32_t)(g->boss.hp>0 ? g->boss.hp:0);
    h.boss_max_hp=(uint32_t)(g->boss.max_hp>0 ? g->boss.max_hp:0);h.boss_attack=g->boss.attack;
    return h;
}
static void initialize_renderer(void)
{
    garden_render_init();garden_render_alpha_support(1U);garden_render_mode(0);
    for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"production framebuffer initialized in host RAM");
    memcpy(base,fb(0),sizeof(base));sel=0;
}
static unsigned draw(Snapshot *s,unsigned buffer)
{
    GardenDraw list[GARDEN_DRAW_MAX];unsigned n=make_draws(s,list);sel=buffer;
    memcpy(expected,base,sizeof(expected));for(unsigned i=0;i<n;i++) oracle_draw(&list[i],1U);
    scope=1;CHECK(garden_render_frame(list,n)==GPU_OK,"production GPU renderer accepted combined actual-core list");scope=0;
    for(unsigned y=GARDEN_Y0;y<GARDEN_Y0+GARDEN_HEIGHT;y++)
        CHECK(!memcmp(&expected[y][GARDEN_X0],fb(sel)+y*DISPLAY_WIDTH+GARDEN_X0,GARDEN_WIDTH*2U),
              "actual combined scene/combat two-FB dirty erase equals independent clean redraw");
    oracle_pixels+=(uint64_t)GARDEN_WIDTH*GARDEN_HEIGHT;
    GardenHud h=hud(&s->game);garden_ui_hud_cached(&h,sel);
    CHECK(garden_stage_draw(&s->stage,&s->game,sel)>=0,"actual Stage footer remains healthy");
    CHECK(!garden_render_stats()->errors && !garden_render_stats()->bad_targets && !garden_render_stats()->atlas_bounds,
          "combined production host rendering has no render/target/atlas errors");
    return n;
}
static void save(const char *prefix,const char *name)
{
    char path[1024];int n=snprintf(path,sizeof(path),"%s_%s.ppm",prefix,name);
    CHECK(n>0 && (unsigned)n<sizeof(path),"output path fits");
    FILE *out=fopen(path,"wb");CHECK(out!=NULL,"PPM output opened");fprintf(out,"P6\n1280 720\n255\n");
    for(unsigned y=0;y<DISPLAY_HEIGHT;y++) for(unsigned x=0;x<DISPLAY_WIDTH;x++) {
        unsigned v=fb(sel)[y*DISPLAY_WIDTH+x],r=(v>>11)&31U,g=(v>>5)&63U,b=v&31U;
        unsigned char rgb[3]={(unsigned char)((r<<3)|(r>>2)),(unsigned char)((g<<2)|(g>>4)),(unsigned char)((b<<3)|(b>>2))};
        CHECK(fwrite(rgb,1,3,out)==3,"RGB565 PPM pixel written");
    }
    CHECK(!fclose(out),"PPM output closed");
}
static unsigned visible(const GardenGame *g)
{
    unsigned n=0;
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) {
        const GardenEnemyBullet *b=&g->enemy_bullets[i];
        n+=b->active && b->x>=0 && b->x<GARDEN_WIDTH*GARDEN_GRAIN && b->y>=0 && b->y<GARDEN_HEIGHT*GARDEN_GRAIN;
    }
    return n;
}
static void output_snapshot(const char *prefix,const char *name,Snapshot *s)
{
    initialize_renderer();garden_stage_ui_reset(&s->stage);unsigned n=draw(s,0);save(prefix,name);
    printf("{\"name\":\"%s\",\"seed\":12345,\"tick\":%u,\"score\":%u,\"mode\":%u,\"boss_hp\":%d,\"boss_attack\":%u,\"visible_bullets\":%u,\"draws\":%u,\"events_this_step\":%u,\"hits_total\":%u,\"kills_total\":%u,\"hit_score_total\":%u,\"kill_score_total\":%u,\"impacts\":%u,\"kill_rings\":%u,\"popups\":%u,\"held_flashes\":%u,\"marker\":%u,\"score_draws\":[",
           name,s->game.tick,s->game.score,s->game.mode,s->game.boss.hp,s->game.boss.attack,
           visible(&s->game),n,s->game.combat.count,s->game.combat.hits_total,s->game.combat.kills_total,
           s->game.combat.hit_score_total,s->game.combat.kill_score_total,s->combat.stats.impacts,
           s->combat.stats.kill_rings,s->combat.stats.popups,s->combat.stats.held_flashes,s->combat.stats.marker);
    GardenDraw list[GARDEN_DRAW_MAX];n=make_draws(s,list);unsigned shown=0;
    for(unsigned i=0;i<n;i++) if(list[i].asset&GARDEN_DRAW_SCORE_BASE) {
        printf("%s{\"award\":%u,\"x\":%d,\"y\":%d,\"alpha\":%u}",shown++ ? ",":"",list[i].asset&0x7FFFU,list[i].x,list[i].y,list[i].alpha);
    }
    puts("]}");
}
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,0x400000U,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"fixed host RAM maps production physical framebuffer and atlas pointers");
    FILE *in=fopen(argv[1],"rb");CHECK(in!=NULL,"actual atlas opened");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES && fgetc(in)==EOF,"exact runtime atlas loaded");fclose(in);
    Snapshot current={0},hit={0},single={0},kill={0},boss={0},death={0},impact_animation[12],kill_animation[12];
    unsigned hit_found=0,single_found=0,kill_found=0,boss_found=0,death_found=0,boss_visible=0,impact_frames=0,kill_frames=0,history_frames=0;
    uint32_t impact_start=UINT32_MAX,kill_start=UINT32_MAX;
    garden_init(&current.game,12345U);garden_fx_reset(&current.fx);garden_stage_reset(&current.stage);
    garden_feedback_reset(&current.feedback);garden_scene_reset(&current.scene);garden_combat_reset(&current.combat);observe(&current);
    initialize_renderer();
    for(unsigned tick=0;tick<8000U;tick++) {
        garden_step(&current.game,0U);observe(&current);
        for(unsigned i=0;i<current.game.combat.count;i++) {
            const GardenHitEvent *e=&current.game.combat.items[i];
            if(!hit_found && !e->boss && !e->killed) { hit=current;hit_found=1U; }
            if(!single_found && current.game.combat.count==1U && !e->killed && e->score_delta==1U) { single=current;single_found=1U; }
            if(!kill_found && !e->boss && e->killed) { kill=current;kill_found=1U;kill_start=current.game.tick; }
            if(e->boss && !e->killed && current.game.boss.hp>1000 && (!boss_found || visible(&current.game)>boss_visible) && garden_stage_boss_alpha(&current.stage,&current.game,1U)==32U) {
                boss=current;boss_found=1U;boss_visible=visible(&current.game);impact_start=current.game.tick;impact_frames=0;
            }
            if(!death_found && e->boss && e->killed) { death=current;death_found=1U; }
        }
        if(impact_start!=UINT32_MAX && impact_frames<12U && current.game.tick==impact_start+impact_frames)
            impact_animation[impact_frames++]=current;
        if(kill_start!=UINT32_MAX && kill_frames<12U && current.game.tick==kill_start+kill_frames)
            kill_animation[kill_frames++]=current;
        if(tick%8U==0U) { draw(&current,(history_frames++)&1U); }
        if(current.game.mode==GARDEN_MODE_VICTORY || current.game.mode==GARDEN_MODE_GAMEOVER) break;
    }
    CHECK(hit_found && single_found && kill_found && boss_found && death_found && impact_frames==12U && kill_frames==12U,
          "real seed supplies enemy hit/kill and Boss hit/death plus contiguous 40-Hz animation frames");
    CHECK(death.game.score==death.game.combat.hit_score_total+death.game.combat.kill_score_total,
          "actual final TOTAL SCORE equals actual hit awards plus retained kill awards");
    printf("PASS: %u alternating production-renderer combined history frames; %llu exact RGB565 playfield pixel comparisons; real score %u = hit %u + kill %u\n",
           history_frames,(unsigned long long)oracle_pixels,death.game.score,death.game.combat.hit_score_total,death.game.combat.kill_score_total);
    output_snapshot(argv[2],"enemy_hit",&hit);output_snapshot(argv[2],"single_hit",&single);output_snapshot(argv[2],"enemy_kill",&kill);
    output_snapshot(argv[2],"boss_hit",&boss);output_snapshot(argv[2],"boss_death",&death);
    for(unsigned i=0;i<12U;i++) {
        char name[40];snprintf(name,sizeof(name),"impact_%u",i);output_snapshot(argv[2],name,&impact_animation[i]);
        snprintf(name,sizeof(name),"kill_%u",i);output_snapshot(argv[2],name,&kill_animation[i]);
    }
    printf("PASS: combat production-renderer host checks=%u; no serial/JTAG/FPGA access\n",checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
