/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production renderer/GPU callback integration for the one changed modal Fill.
 * Existing callbacks map real addresses, render real RGB565 atlas pixels and
 * enforce production command bounds. The old complete test main is not run.
 */
#define main settlement_unrun_renderer_suite
#include "test_render_batch.c"
#undef main
#include "garden_stage.h"

static FILE *snapshot_file;
static unsigned compare_mode,snapshot_count,first_modal_count;
static uint64_t first_fill_pixels[2],first_blit_pixels[2];
static unsigned first_fills[2],first_blits[2];
static uint64_t image_digest=1469598103934665603ULL;
static uint16_t reference_snapshot[DISPLAY_HEIGHT][DISPLAY_WIDTH];

static void snapshot(void)
{
    size_t bytes=sizeof(reference_snapshot);
    if(compare_mode) {
        CHECK(fread(reference_snapshot,1,bytes,snapshot_file)==bytes,"old renderer snapshot contains complete framebuffer");
        CHECK(!memcmp(reference_snapshot,fb(sel),bytes),"new modal exact whole RGB565 framebuffer equals old modal");
    } else CHECK(fwrite(fb(sel),1,bytes,snapshot_file)==bytes,"seal old whole RGB565 framebuffer");
    /* Full-frame digest is supplemental: actual equality above usesmemcmp. */
    const uint16_t *pixels=fb(sel);
    for(unsigned i=0;i<DISPLAY_WIDTH*DISPLAY_HEIGHT;i++)
        image_digest=(image_digest^pixels[i])*1099511628211ULL;
    snapshot_count++;
}

static void initialize(GardenStage *stage,GardenGame *game)
{
    garden_render_init();garden_render_alpha_support(1);
    for(sel=0;sel<2U;sel++) CHECK(garden_render_frame(NULL,0U)==GPU_OK,"initialize each real production backbuffer");
    memset(game,0,sizeof(*game));garden_stage_reset(stage);
    game->tick=100U;game->mode=GARDEN_MODE_BOSS;game->boss.active=1U;
    game->boss.x=320000;game->boss.y=100000;game->score=950U;
    game->demo_mode=1U;game->lives=2U;game->reward.high_score=2220U;
    game->reward.run_ticks=3894U;game->reward.hits_taken=1U;game->reward.graze_count=7U;
    game->reward.items_collected=10U;game->reward.bombs_used=1U;
    garden_stage_observe(stage,game);
    game->tick=101U;game->mode=GARDEN_MODE_VICTORY;game->boss.active=0U;
    game->score=2195U;garden_stage_observe(stage,game);
}

static void frame(GardenStage *stage,GardenGame *game,unsigned buffer,unsigned initial_modal)
{
    sel=buffer;GardenDraw scene[16];unsigned count=0U;
    if(!garden_stage_modal_active(stage,game)) {
        scene[count++]=(GardenDraw){320,410,GARDEN_ASSET_PLAYER,32U};
        count=garden_stage_append_effects(stage,game,scene,count,16U,1U);
    }
    CHECK(garden_stage_prepare_frame(stage,game,buffer)>=0,"prepare modal/restart cleanup before scene");
    CHECK(garden_render_frame(scene,count)==GPU_OK,"production two-FB dirty history before stage overlay");
    GardenHud hud={0};hud.state=game->mode==GARDEN_MODE_VICTORY ? "STAGE CLEAR":"STAGE 1 WAVES";
    hud.score=garden_stage_visible_score(stage,game);hud.lives=game->lives;
    hud.demo=game->demo_mode;hud.alpha_supported=hud.alpha_enabled=1U;
    hud.fps10=601U;hud.gpu_bench_us=23000U;hud.cpu_bench_us=112000U;
    garden_ui_hud_cached(&hud,buffer);
    trace_on=1U;trace_fill_count=trace_blit_count=0U;
    CHECK(garden_stage_draw(stage,game,buffer)>=0,"production old/new modal rendering succeeds");
    trace_on=0U;
    if(initial_modal) {
        first_modal_count++;
        first_fills[buffer]=trace_fill_count;first_blits[buffer]=trace_blit_count;
        for(unsigned i=0;i<trace_fill_count;i++)first_fill_pixels[buffer]+=(uint64_t)trace_fills[i].w*trace_fills[i].h;
        for(unsigned i=0;i<trace_blit_count;i++)first_blit_pixels[buffer]+=(uint64_t)trace_blits[i].w*trace_blits[i].h;
        printf("FIRST_MODAL fb=%u fill_commands=%u fill_pixels=%llu blit_commands=%u blit_pixels=%llu\n",
            buffer,first_fills[buffer],(unsigned long long)first_fill_pixels[buffer],
            first_blits[buffer],(unsigned long long)first_blit_pixels[buffer]);
    }
    unsigned before=calls;
    CHECK(garden_stage_draw(stage,game,buffer)==0 && calls==before,"same-tick/same-FB modal or footer is fully cached");
    snapshot();
}

int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    compare_mode=!strcmp(argv[2],"compare");
    CHECK(compare_mode || !strcmp(argv[2],"write"),"explicit old snapshot or candidate compare mode");
    snapshot_file=fopen(argv[3],compare_mode ? "rb":"wb");CHECK(snapshot_file!=NULL,"open bounded temporary reference snapshot stream");
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,(ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"real framebuffer/atlas addresses mapped only in host RAM");
    FILE *atlas=fopen(argv[1],"rb");CHECK(atlas!=NULL,"real frozen atlas opened");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,atlas)==GARDEN_ATLAS_BYTES && fgetc(atlas)==EOF,"load exact real atlas");fclose(atlas);
    GardenStage stage;GardenGame game;initialize(&stage,&game);
    static const unsigned ages[]={99U,100U,101U,120U,140U,179U,180U,219U,220U,319U};
    for(unsigned i=0;i<sizeof(ages)/sizeof(ages[0]);i++) {
        game.tick=stage.victory_tick+ages[i];
        if(ages[i]>=120U)game.score=2220U; /* actual late pickup credit */
        garden_stage_observe(&stage,&game);
        for(unsigned b=0;b<2U;b++)frame(&stage,&game,b,ages[i]==100U);
        CHECK(garden_stage_visible_score(&stage,&game)<=game.score,"presentation cannot overshoot genuine credit");
    }
    CHECK(first_modal_count==2U && stage.modal_buffers==3U,"both physical buffers really displayed first result cards");
    uint32_t before_clears=stage.stats.modal_clears;
    game.tick=0U;game.mode=GARDEN_MODE_WAVES;game.score=0U;game.lives=3U;
    garden_stage_observe(&stage,&game);
    for(unsigned b=0;b<2U;b++)frame(&stage,&game,b,0U);
    CHECK(stage.modal_buffers==0U && stage.stats.modal_clears==before_clears+2U,
          "restart clears each former modal through production renderer before fresh objects");
    if(compare_mode)CHECK(fgetc(snapshot_file)==EOF,"all old snapshots compared without missing or trailing frames");
    fclose(snapshot_file);VirtualFree(ram,0,MEM_RELEASE);
    printf("SETTLEMENT_FILL snapshots=%u digest=0x%llx first_modal_count=%u checks=%u\n",
        snapshot_count,(unsigned long long)image_digest,first_modal_count,checks);
    return 0;
}

