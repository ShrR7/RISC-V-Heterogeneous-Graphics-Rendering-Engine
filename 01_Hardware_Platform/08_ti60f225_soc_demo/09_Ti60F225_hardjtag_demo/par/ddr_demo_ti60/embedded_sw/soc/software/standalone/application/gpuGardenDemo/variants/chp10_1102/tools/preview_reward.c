/* SPDX-License-Identifier: GPL-3.0-or-later
 * HOST PREVIEW ONLY. Real core seed12345 and production renderer on host RAM.
 * No board, UART, FPGA timing or fabricated gameplay snapshots. */
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
#include "garden_core.h"
#include "garden_fx.h"
#include "garden_feedback.h"
#include "garden_scene.h"
#include "garden_combat.h"
#include "garden_stage.h"
#include "garden_actor_ui.h"
#include "garden_animation.h"
#include "garden_motion.h"

static unsigned checks,sel;
#define CHECK(c,m) do { checks++;if(!(c)) { fprintf(stderr,"FAIL %u: %s\n",__LINE__,m);exit(2); } } while(0)
static uint16_t *fb(unsigned b) { return (uint16_t *)(uintptr_t)(b ? FB1_ADDR:FB0_ADDR); }
uint32_t display_back_sel(void) { return sel; }
uint32_t display_back_addr(void) { return sel ? FB1_ADDR:FB0_ADDR; }
int display_check_render_target(uint32_t a)
{ uint32_t base=display_back_addr();return a>=base && a<base+FB_SIZE_BYTES ? GPU_OK:GPU_ESTATE; }
void garden_render_host_fence(void) {}
void data_cache_invalidate_all(void) {}
static void bounds(uint32_t dst,unsigned w,unsigned h,unsigned stride)
{
    uint32_t off=dst-display_back_addr();
    unsigned x=(off%DISPLAY_STRIDE_BYTES)/2U,y=off/DISPLAY_STRIDE_BYTES;
    CHECK(stride==DISPLAY_STRIDE_BYTES && (w>128U ? h<=16U:(h<=64U && (uint64_t)w*h<=4096U)),"actual renderer command budget");
    CHECK(display_check_render_target(dst)==GPU_OK && x+w<=DISPLAY_WIDTH && y+h<=DISPLAY_HEIGHT,"only selected physical back buffer");
}
int gpu_fill(uint32_t dst,uint32_t w,uint32_t h,uint32_t stride,uint16_t color)
{
    bounds(dst,w,h,stride);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) *(uint16_t *)(uintptr_t)(dst+y*stride+x*2U)=color;
    return GPU_OK;
}
static int blit(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t key,unsigned alpha)
{
    bounds(dst,w,h,ds);CHECK(alpha<=32U,"real alpha range");
    CHECK(src>=ASSET_BASE_ADDR && (uint64_t)src+(h-1U)*ss+w*2U<=ASSET_BASE_ADDR+GARDEN_ATLAS_BYTES,"actual atlas source bounds");
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint16_t v=*(uint16_t *)(uintptr_t)(src+y*ss+x*2U),*out=(uint16_t *)(uintptr_t)(dst+y*ds+x*2U);
        if(v!=key) *out=alpha==32U ? v:gpu_alpha_rgb565(v,*out,alpha);
    }
    return GPU_OK;
}
int gpu_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k)
{ return blit(s,d,w,h,ss,ds,k,32U); }
int gpu_alpha_bitblt(uint32_t s,uint32_t d,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{ return blit(s,d,w,h,ss,ds,k,a); }

typedef struct {
    GardenGame game;GardenFx fx;GardenFeedback feedback;GardenScene scene;
    GardenCombat combat;GardenStage stage;GardenActorUi actor;GardenAnimation animation;GardenMotion motion;
} Snapshot;
static void observe(Snapshot *s)
{
    garden_motion_observe(&s->motion,&s->game);garden_fx_observe(&s->fx,&s->game);
    garden_stage_observe(&s->stage,&s->game);garden_feedback_observe(&s->feedback,&s->game);
    garden_scene_observe(&s->scene,&s->game);garden_combat_observe(&s->combat,&s->game);
    garden_actor_ui_observe(&s->actor,&s->game);garden_animation_observe(&s->animation,&s->game);
}
/* This is main.make_draws in the same order, with slow=0, Alpha enabled and
 * its diagnostic panel hidden. Coordinates are WORLD CENTRES; the production
 * renderer adds field origin and subtracts each actual atlas anchor once. */
static unsigned make_draws(Snapshot *s,GardenDraw *out)
{
    const GardenGame *g=&s->game;
    if(garden_stage_modal_active(&s->stage,g)) return 0U;
    unsigned capacity=GARDEN_DRAW_MAX,essential=g->boss.active!=0U;
    essential+=g->player.active && (!g->player.grace || (g->tick&3U)<2U);
    for(unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) essential+=g->enemies[i].active!=0U;
    for(unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) essential+=g->player_bullets[i].active!=0U;
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) essential+=g->enemy_bullets[i].active!=0U;
    for(unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) essential+=g->effects[i].active!=0U;
    essential+=g->demo_mode && g->player.active && !g->player.respawning;
    unsigned reserve=garden_actor_ui_bar_count(g),free_slots=capacity>essential ? capacity-essential:0U;
    if(reserve>free_slots) reserve=free_slots;
    unsigned n=garden_combat_build(&s->combat,&s->scene,&s->feedback,&s->fx,g,out,capacity-reserve,1U,0U);
    unsigned alpha=garden_stage_boss_alpha(&s->stage,g,1U);
    for(unsigned i=0;i<n;i++) {
        unsigned id=out[i].asset;
        if(id==GARDEN_ASSET_BOSS1 || id==GARDEN_ASSET_FLASH_BOSS1) out[i].alpha=(uint8_t)alpha;
        else if(id==GARDEN_ASSET_BOSS_WING_L || id==GARDEN_ASSET_BOSS_WING_R || id==GARDEN_ASSET_BOSS_CORE || id==GARDEN_ASSET_BOSS_CHARGE)
            if(out[i].alpha>alpha) out[i].alpha=(uint8_t)alpha;
    }
    garden_actor_ui_apply(&s->actor,g,out,n);
    n=garden_actor_ui_append_bars(&s->actor,g,out,n,capacity);
    (void)garden_motion_apply_bullets(&s->motion,g,out,n);
    n=garden_animation_apply(&s->animation,g,out,n,capacity,1U);
    for(unsigned i=0;i<GARDEN_MAX_PICKUPS && n+2U<capacity;i++) {
        const GardenPickup *it=&g->pickups[i];if(!it->active) continue;
        out[n++]=(GardenDraw){(int16_t)(it->x/GARDEN_GRAIN),(int16_t)(it->y/GARDEN_GRAIN),GARDEN_ASSET_HIT_SPARK,32U};
    }
    if(g->reward.pickup_flash_ticks && n<capacity) out[n++]=(GardenDraw){
        (int16_t)(g->reward.pickup_flash_x/GARDEN_GRAIN),(int16_t)(g->reward.pickup_flash_y/GARDEN_GRAIN),
        GARDEN_ASSET_FX_RING32,(uint8_t)(g->reward.pickup_flash_ticks*2U)};
    CHECK(!s->combat.stats.essential_dropped,"no essential scene draw dropped");
    return garden_stage_append_effects(&s->stage,g,out,n,capacity,1U);
}
static void initialize_renderer(Snapshot *s)
{
    garden_render_init();garden_render_alpha_support(1U);garden_render_mode(0);
    garden_stage_ui_reset(&s->stage);s->stage.modal_buffers=0U;
    for(sel=0;sel<2;sel++) CHECK(garden_render_frame(NULL,0)==GPU_OK,"initialize both actual renderer buffers");
    sel=0;
}
static unsigned draw(Snapshot *s,unsigned buffer)
{
    GardenDraw list[GARDEN_DRAW_MAX];unsigned n=make_draws(s,list);sel=buffer;
    CHECK(garden_stage_prepare_frame(&s->stage,&s->game,sel)>=0,"prepare previous modal before scene");
    CHECK(garden_render_frame(list,n)==GPU_OK,"production renderer real scene");
    const GardenGame *g=&s->game;
    GardenHud h={.state=g->mode==GARDEN_MODE_VICTORY ? "STAGE CLEAR":g->boss.active ? "STAGE 1 BOSS":"STAGE 1 WAVES",
        .score=garden_stage_visible_score(&s->stage,g),.lives=g->lives,.high_score=g->reward.high_score,
        .bombs=g->reward.bombs,.graze=g->reward.graze_count,.items=g->reward.items_collected,.phase=g->boss.hp_band,
        .bullets=g->enemy_bullet_count,.peak=g->enemy_bullet_peak,.demo=g->demo_mode,.alpha_supported=1,.alpha_enabled=1,.alpha_level=16,
        .boss_hp=(uint32_t)(g->boss.hp>0 ? g->boss.hp:0),.boss_max_hp=(uint32_t)g->boss.max_hp,.boss_attack=g->boss.attack,.boss_active=g->boss.active};
    /* Diagnostics stay hidden: no fake sample FPS or GPU timing. */
    garden_ui_hud_cached(&h,sel);garden_ui_score_cached(&h,sel);
    CHECK(garden_stage_draw(&s->stage,g,sel)>=0,"draw footer or real settlement afterscene");
    const GardenRenderStats *rs=garden_render_stats();
    CHECK(!rs->errors && !rs->bad_targets && !rs->atlas_bounds,"no host render/target/atlas errors");
    return n;
}
static void save(const char *prefix,const char *name,const Snapshot *s,unsigned draws)
{
    char path[1024];CHECK(snprintf(path,sizeof(path),"%s_%s.ppm",prefix,name)>0,"snapshot path");
    FILE *out=fopen(path,"wb");CHECK(out!=NULL,"open PPM");fprintf(out,"P6\n1280 720\n255\n");
    for(unsigned y=0;y<DISPLAY_HEIGHT;y++) for(unsigned x=0;x<DISPLAY_WIDTH;x++) {
        unsigned v=fb(sel)[y*DISPLAY_WIDTH+x],r=(v>>11)&31U,g=(v>>5)&63U,b=v&31U;
        unsigned char rgb[3]={(unsigned char)((r<<3)|(r>>2)),(unsigned char)((g<<2)|(g>>4)),(unsigned char)((b<<3)|(b>>2))};
        CHECK(fwrite(rgb,1,3,out)==3,"write RGB565 actual pixel");
    }
    fclose(out);
    printf("{\"name\":\"%s\",\"seed\":12345,\"tick\":%u,\"mode\":%u,\"score\":%u,\"visible_score\":%u,\"high_score\":%u,\"graze\":%u,\"items\":%u,\"hits\":%u,\"bombs_used\":%u,\"bomb_flash_ticks\":%u,\"pickup_count\":%u,\"eb\":%u,\"draws\":%u,\"victory_age\":%u}\n",
        name,s->game.tick,s->game.mode,s->game.score,garden_stage_visible_score(&s->stage,&s->game),
        s->game.reward.high_score,s->game.reward.graze_count,s->game.reward.items_collected,s->game.reward.hits_taken,
        s->game.reward.bombs_used,s->game.reward.bomb_flash_ticks,s->game.reward.pickup_count,s->game.enemy_bullet_count,draws,
        s->game.mode==GARDEN_MODE_VICTORY ? s->game.tick-s->stage.victory_tick:0U);
}
static void snapshot(const char *prefix,const char *name,Snapshot source)
{ initialize_renderer(&source);unsigned n=draw(&source,0U);save(prefix,name,&source,n); }
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"host fixed RAM maps actual production addresses");
    FILE *atlas=fopen(argv[1],"rb");CHECK(atlas!=NULL,"open unchanged atlas");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,atlas)==GARDEN_ATLAS_BYTES && fgetc(atlas)==EOF,"exact runtime atlas bytes");fclose(atlas);
    Snapshot s={0},battle={0},bomb={0},pickup={0},victory={0};
    unsigned best_bullets=0,bomb_seen=0,pickup_seen=0,victory_seen=0;
    garden_init(&s.game,12345U);garden_motion_reset(&s.motion);garden_fx_reset(&s.fx);
    garden_fx_set_flags(&s.fx,GARDEN_FX_FLASH|GARDEN_FX_RING);garden_stage_reset(&s.stage);
    garden_feedback_reset(&s.feedback);garden_scene_reset(&s.scene);garden_combat_reset(&s.combat);
    garden_actor_ui_reset(&s.actor);garden_animation_reset(&s.animation);observe(&s);
    for(unsigned step=0;step<8000;step++) {
        garden_step(&s.game,0U);observe(&s);
        if(!bomb_seen && s.game.reward.bomb_flash_ticks==12U) { bomb=s;bomb_seen=1; }
        if(s.game.reward.pickup_count && !pickup_seen) { pickup=s;pickup_seen=1; }
        if(s.game.boss.active && s.game.enemy_bullet_count>best_bullets) { battle=s;best_bullets=s.game.enemy_bullet_count; }
        if(s.game.mode==GARDEN_MODE_VICTORY) { victory=s;victory_seen=1;break; }
        CHECK(s.game.mode!=GARDEN_MODE_GAMEOVER,"deterministic shipped seed survives");
    }
    CHECK(victory_seen && victory.game.tick==3894U,"unmodified seed12345 actualvictory tick3894");
    CHECK(bomb_seen && pickup_seen && best_bullets,"actual bomb,pickup,andbattle captured");
    snapshot(argv[2],"battle",battle);snapshot(argv[2],"bomb",bomb);snapshot(argv[2],"pickup",pickup);
    unsigned sample_ages[]={0,20,45,65,85,99,100,140,180,220};unsigned next=0;
    s=victory;
    for(unsigned age=0;age<=220;age++) {
        if(age) { garden_step(&s.game,0U);observe(&s); }
        if(next<sizeof(sample_ages)/sizeof(*sample_ages) && age==sample_ages[next]) {
            char name[32];snprintf(name,sizeof(name),age==220 ? "settlement":"death_%03u",age);
            snapshot(argv[2],name,s);next++;
        }
    }
    CHECK(s.game.score==2220U && s.game.reward.high_score==2220U,"actual final reward credit2220 andsessionbest");
    CHECK(garden_stage_visible_score(&s.stage,&s.game)==2220U,"visible final reward exactcredit");
    s=victory;initialize_renderer(&s);unsigned age=0;
    for(unsigned frame=0;frame<120;frame++) {
        while(age<frame*2U) { garden_step(&s.game,0U);observe(&s);age++; }
        char name[32];snprintf(name,sizeof(name),"anim_%03u",frame);
        unsigned n=draw(&s,frame&1U);save(argv[2],name,&s,n);
    }
    printf("PASS HOST_PREVIEW checks=%u victory_tick=3894 final_score=2220 no_board=1\n",checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
