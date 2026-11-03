/* SPDX-License-Identifier: GPL-3.0-or-later
 * Garden of Coloured Lights 1.0.9, reduced stage/Boss port for Ti60.
 * Original game: Linley Henzell; see COPYING and tools/core_port_notes.md.
 */
#include <stdint.h>
#include <string.h>
#include "bsp.h"
#include "vexriscv.h"
#include "gpu_regs.h"
#include "gpu_driver.h"
#include "gpu_alpha_verify.h"
#include "gpu_precision_verify.h"
#include "gpu_display.h"
#include "display_debug.h"
#include "garden_core.h"
#include "garden_clear.h"
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "frame_profile.h"
#include "garden_log.h"
#include "garden_fx.h"
#include "garden_stage.h"
#include "garden_feedback.h"
#include "garden_scene.h"
#include "garden_combat.h"
#include "garden_actor_ui.h"
#include "garden_animation.h"
#include "garden_motion.h"
#include "garden_ordnance_view.h"
#include "g9_memory.h"
#include "g9_route.h"
#include "stage9_link.h"
#include "g9_workload.h"
#include "pure_bullet_test.h"
#include "pad_hw.h"
#include "garden_controller.h"
#include "garden_session.h"

#define SWAP_TIMEOUT 20000000U
#ifndef GARDEN_FIXED_RUN_SEED
/* Nonzero override supports a repeatable first-round bring-up build. */
#define GARDEN_FIXED_RUN_SEED 0U
#endif
#define game G9_GAME
static GardenFx fx;
static GardenStage stage;
static GardenFeedback feedback;
static GardenScene scene;
static GardenCombat combat;
static GardenActorUi actor_ui;
static GardenAnimation animation;
#define motion G9_MOTION
static uint32_t score_hud_previous=UINT32_MAX,score_hud_calls;
static unsigned score_hud_dirty[2]={1,1};
static uint32_t boss_fade_frames;
#define draws G9_DRAWS
static unsigned g9_enabled=0U,g9_started;
static uint32_t g9_prev_shown,g9_prev_engine,g9_prev_px,g9_prev_lines,g9_prev_reject,g9_prev_skip;
static uint32_t g9_min_actual=UINT32_MAX,g9_max_actual,g9_min_migrated=UINT32_MAX,g9_max_migrated;
static uint64_t g9_sum_migrated,g9_sum_legacy,g9_sum_desc;
static uint32_t ticks_per_second;
static uint32_t interval_fps10, interval_logic_us, interval_render_us;
static uint32_t errors, missed_logic;
static uint32_t gpu_over_budget,cpu_over_budget;
static uint16_t pending_events, direction, controller_held;
static uint64_t direction_until;
static uint64_t bomb_key_until;
static unsigned slow, pure_test_requested, boss_test_requested;
static GardenSession session;
static unsigned input_report_pending;
static char last_input;
static unsigned hud_dirty[2]={1,1};
static unsigned periodic_logs=1U,diagnostics=1U;
static unsigned async_logs=1U;
static unsigned log_tx_free(void) { return uart_writeAvailability(BSP_UART_TERMINAL); }
static void log_tx_put(char c) { uart_write(BSP_UART_TERMINAL,c); }
static uint32_t log_errors(void)
{ const GardenLogStats *s=garden_log_stats(); return s->dropped_lines+s->truncated_lines; }

void trap_entry(void);
void trap(void)
{
    bsp_printf("\r\n[TRAP] mcause=0x%x mepc=0x%x mtval=0x%x\r\n",(uint32_t)csr_read(mcause),(uint32_t)csr_read(mepc),(uint32_t)csr_read(mtval));
    while (1) { }
}
static uint32_t now32(void) { return clint_getTimeLow(BSP_CLINT); }
static uint64_t now64(void) { return clint_getTime(BSP_CLINT); }
/* Keep each clear piece small and leave DDR a quiet interval. This is a
 * board scheduling experiment, not a hardware bandwidth guarantee. No game,
 * renderer, input or log code may reenter while the Game is partly cleared. */
static void clear_yield(void)
{
    asm volatile("fence iorw,iorw" ::: "memory");
    uint32_t begin=now32();
    uint32_t wait=(ticks_per_second+499999U)/500000U;
    while((uint32_t)(now32()-begin)<wait) { asm volatile("nop"); }
}
static void clear_report(const char *phase,uint32_t round)
{
    const GardenClearStats *s=garden_clear_get_stats();
    garden_log_printf("[CLEAR] phase=%s round=%u tick=%u mode=%u calls=%u bytes=%u chunks=%u yields=%u max_chunk=%u wait_us=2 scan=%u\r\n",
        phase,round,game.tick,game.mode,s->calls,s->bytes,s->chunks,s->yields,
        s->max_chunk,garden_enemy_scan_limit);
}
static uint32_t microseconds(uint64_t t) { return (uint32_t)(t*1000000ULL/ticks_per_second); }
static void fail(const char *why)
{
    /* Fatal paths must drain accepted records before halting the frame pump. */
    garden_log_set_async(0U);
    Stage9LinkStats ds;stage9_link_snapshot(&ds);
    garden_log_printf("[G9_FATAL] reason=%s error=%d raw_status=0x%x raw_px=%u raw_lines=%u reject=%u submissions=%u guard=%u\r\n",why,ds.last_error,ds.status,ds.missing_pixels,ds.missing_lines,ds.rejected,ds.submissions,g9_memory_guard_check());
    garden_log_printf("RESULT: GARDEN FAIL %s\r\n",why);
    while (1) { }
}
static uint32_t atlas_crc(void)
{
    const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)ASSET_BASE_ADDR;
    uint32_t crc=0xFFFFFFFFU;
    for (unsigned i=0;i<GARDEN_ATLAS_BYTES;i++) {
        crc^=p[i];
        for (unsigned b=0;b<8;b++) crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320U);
    }
    return crc^0xFFFFFFFFU;
}
static void calibrate(void)
{
    unsigned active=display_get_active();
    /* CLINT is clocked by the design's 100 MHz user clock. Display swap
     * periods are measured separately, not used to define a second. */
    ticks_per_second=BSP_CLINT_HZ;
    if (ticks_per_second<1000000U) fail("timer frequency");
    active^=1U; display_request_fb(active);
    if (display_wait_swap(active,SWAP_TIMEOUT)!=GPU_OK) fail("swap align");
    uint32_t t=now32();
    for (unsigned i=0;i<60;i++) {
        active^=1U; display_request_fb(active);
        if (display_wait_swap(active,SWAP_TIMEOUT)!=GPU_OK) fail("swap cal");
    }
    uint32_t span=now32()-t;
    if (span<ticks_per_second/2U || span>ticks_per_second*2U) fail("display period");

    garden_log_printf("[CAL] ticks_per_second=%d logic_hz=%d source=BSP_CLINT full_periods=60 aligned=1 swap60_ticks=%d display_hz10=%d\r\n",ticks_per_second,GARDEN_LOGIC_HZ,span,(uint32_t)(600ULL*ticks_per_second/span));
}
static unsigned make_draws(void)
{
    unsigned focus=slow || (controller_held&GARDEN_INPUT_SLOW);
    if(garden_stage_modal_active(&stage,&game)) return 0U;
    unsigned full_capacity=GARDEN_DRAW_MAX-((garden_render_alpha_enabled() &&
                                       garden_render_panel_is_visible()) ? 1U : 0U);
    unsigned extra=garden_ordnance_view_reserve(&game);
    unsigned capacity=full_capacity>extra ? full_capacity-extra : 0U;
    unsigned essential=game.boss.active!=0U;
    essential+=game.player.active && (!game.player.grace || (game.tick&3U)<2U);
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) essential+=game.enemies[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) essential+=game.player_bullets[i].active!=0U;
    for (unsigned i=0;i<garden_enemy_scan_limit;i++) essential+=game.enemy_bullets[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) essential+=game.effects[i].active!=0U;
    essential+=(focus || game.demo_mode) && game.player.active && !game.player.respawning;
    unsigned reserve=garden_actor_ui_bar_count(&game);
    unsigned free_slots=capacity>essential ? capacity-essential : 0U;
    if (reserve>free_slots) reserve=free_slots;
    unsigned n=garden_combat_build(&combat,&scene,&feedback,&fx,&game,draws,capacity-reserve,
                                     (unsigned)garden_render_alpha_enabled(),focus);
    unsigned alpha=garden_stage_boss_alpha(&stage,&game,(unsigned)garden_render_alpha_enabled());
    for (unsigned i=0;i<n;i++) if (draws[i].asset==GARDEN_ASSET_BOSS1 ||
                                  draws[i].asset==GARDEN_ASSET_FLASH_BOSS1) {
        draws[i].alpha=(uint8_t)alpha;
        if (alpha<32U) boss_fade_frames++;
    } else if (draws[i].asset==GARDEN_ASSET_BOSS_WING_L ||
               draws[i].asset==GARDEN_ASSET_BOSS_WING_R ||
               draws[i].asset==GARDEN_ASSET_BOSS_CORE ||
               draws[i].asset==GARDEN_ASSET_BOSS_CHARGE) {
        /* The entire decorative assembly shares the original entrance fade;
         * never brighten a charge cue above its own chosen opacity. */
        if (draws[i].alpha>alpha) draws[i].alpha=(uint8_t)alpha;
    }
    garden_actor_ui_apply(&actor_ui,&game,draws,n);
    n=garden_actor_ui_append_bars(&actor_ui,&game,draws,n,capacity);
    (void)garden_motion_apply_bullets(&motion,&game,draws,n);
    n=garden_animation_apply(&animation,&game,draws,n,capacity,
                             (unsigned)garden_render_alpha_enabled());
    errors+=garden_combat_stats(&combat)->essential_dropped;
    n=garden_ordnance_view_build(&game,draws,n,full_capacity,
                                 (unsigned)garden_render_alpha_enabled());
    n=garden_stage_append_effects(&stage,&game,draws,n,full_capacity,
                                 (unsigned)garden_render_alpha_enabled());
    garden_ordnance_camera_apply(&game,draws,n);
    return n;
}
static const char *mode_name(void)
{
    if (game.paused) return "PAUSED";
    switch (game.mode) {
        case GARDEN_MODE_WAVES: return "STAGE 1 WAVES";
        case GARDEN_MODE_BOSS: return "STAGE 1 BOSS";
        case GARDEN_MODE_VICTORY: return "STAGE CLEAR";
        default: return "GAME OVER / MINUS RESTART";
    }
}
static int hud(void)
{
    const GardenRenderStats *render=garden_render_stats();
    uint32_t before=render->errors+render->bad_targets+render->atlas_bounds;
    const GardenHud h={
        .state=mode_name(), .score=garden_stage_visible_score(&stage,&game), .lives=game.lives,
        .high_score=game.reward.high_score,.bombs=game.reward.bombs,.graze=game.reward.graze_count,
        .items=game.reward.items_collected,.phase=game.boss.hp_band,.diagnostics=diagnostics,
        .bullets=game.enemy_bullet_count, .peak=game.enemy_bullet_peak,
        .fps10=interval_fps10, .render_us=interval_render_us, .logic_us=interval_logic_us,
        .gpu_bench_us=0U, .cpu_bench_us=0U,
        .errors=errors+garden_render_stats()->errors+log_errors(),
        .boss_hp=(uint32_t)(game.boss.hp>0 ? game.boss.hp : 0),
        .boss_max_hp=(uint32_t)(game.boss.max_hp>0 ? game.boss.max_hp : 0),
        .boss_attack=game.boss.attack, .boss_active=game.boss.active,
        .cpu=(unsigned)garden_render_is_cpu(), .demo=game.demo_mode,
        .alpha_supported=(unsigned)garden_render_alpha_supported(),
        .alpha_enabled=(unsigned)garden_render_alpha_enabled(),
        .alpha_level=garden_render_alpha_level()
    };
    garden_ui_hud_cached(&h,display_back_sel());
    /* CPU HUD writes share the same publication requirement as sprites. */
    if (garden_render_is_cpu()) { asm volatile("fence rw,rw" ::: "memory"); data_cache_invalidate_all(); }
    if (before!=render->errors+render->bad_targets+render->atlas_bounds) {
        garden_ui_hud_reset();
        return 0;
    }
    return 1;
}
static int score_hud(void)
{
    const GardenRenderStats *r=garden_render_stats();
    uint32_t before=r->errors+r->bad_targets+r->atlas_bounds;
    const GardenHud h={.score=garden_stage_visible_score(&stage,&game)};
    garden_ui_score_cached(&h,display_back_sel());
    score_hud_calls++;
    if (before!=r->errors+r->bad_targets+r->atlas_bounds) {
        garden_ui_hud_reset();
        score_hud_dirty[0]=score_hud_dirty[1]=1U;
        return 0;
    }
    return 1;
}
static void poll_input(uint64_t clock)
{
    while (uart_readOccupancy(BSP_UART_TERMINAL)) {
        char c=uart_read(BSP_UART_TERMINAL);
        if (c>='A' && c<='Z') c+=(char)('a'-'A');
        uint16_t bit=0;
        switch (c) {
            case 'i': garden_log_printf("[ENGINEERING_UI] always_visible=1\r\n");continue;
            case 'l':
                periodic_logs^=1U;
                garden_log_printf("[FRAME_CTRL] verbose=%d profile=%d async=%d\r\n",periodic_logs,0U,async_logs);
                continue;
            case 'y':
                periodic_logs^=2U;
                garden_log_printf("[DETAIL_CTRL] enabled=%u\r\n",(periodic_logs>>1)&1U);
                continue;
            case 'm':
                async_logs^=1U; garden_log_set_async(async_logs);
                garden_log_printf("[FRAME_CTRL] async=%d verbose=%d profile=%d\r\n",async_logs,periodic_logs,0U);
                continue;
            case 'a': bit=GARDEN_INPUT_LEFT; break;
            case 'd': bit=GARDEN_INPUT_RIGHT; break;
            case 'w': bit=GARDEN_INPUT_UP; break;
            case 's': bit=GARDEN_INPUT_DOWN; break;
            case 'q':
                if(clock<bomb_key_until)continue;
                bomb_key_until=clock+ticks_per_second/2U;
                pending_events|=GARDEN_INPUT_BOMB;break;
            case 'f': slow^=1U; break;
            case 'p': pending_events^=GARDEN_INPUT_PAUSE; break;
            case 'r': case ' ': case '\r': pending_events|=GARDEN_INPUT_RESTART; break;
            case 't': pure_test_requested=1U; continue;
            case 'u': pending_events^=GARDEN_INPUT_TOGGLE_DEMO; break;
            case 'c': garden_render_mode(!garden_render_is_cpu()); break;
            case 'j': garden_render_batch_mode(!garden_render_is_batch()); garden_log_printf("[DRAW_PATH] batch=%u\r\n",garden_render_is_batch()); break;
            case 'b': boss_test_requested=1U;continue;
            case 'k': g9_enabled^=1U;garden_log_printf("[G9_CTRL] enabled=%u target=%u\r\n",g9_enabled,g9_workload_get_target());continue;
            case '0': case '1': case '2': case '3': case '4': case '5': {
                static const unsigned targets[6]={0U,128U,512U,1024U,1500U,2000U};
                g9_workload_set(&game,targets[(unsigned)(c-'0')]);
                garden_log_printf("[G9_WORKLOAD] target=%u injected=1 normal_collision=1 normal_logic=1\r\n",g9_workload_get_target());
                continue;
            }
            case 'v':
                if (garden_render_alpha_toggle()!=GPU_OK) garden_log_printf("[ALPHA] unavailable\r\n");
                else garden_log_printf("[ALPHA] enabled=%d\r\n",garden_render_alpha_enabled());
                break;
            case 'n': garden_render_alpha_cycle(); break;
            case 'h':
                garden_combat_set_enabled(&combat,!combat.enabled);
                garden_log_printf("[COMBAT_CTRL] enabled=%d hit_score=1 kill_rewards=20/80/1000 scorerule_always_active=1\r\n",combat.enabled);
                break;
            case 'g':
                garden_scene_set_enabled(&scene,!scene.enabled);
                garden_log_printf("[SCENE_CTRL] enabled=%d scene_presentation_only=1 real_rhythm=1 charge_ticks_by_band=20/16/12 background_max=8\r\n",scene.enabled);
                break;
            case 'z':
                garden_stage_set_enabled(&stage,!stage.enabled);
                garden_log_printf("[STAGE_CTRL] enabled=%d warning_ticks=100 fade_ticks=64 sound=0\r\n",stage.enabled);
                break;
            case 'x': {
                unsigned flags=fx.flags==GARDEN_FX_ALL ? 0U :
                    (fx.flags==0U ? GARDEN_FX_FLASH :
                    (fx.flags==GARDEN_FX_FLASH ? GARDEN_FX_FLASH|GARDEN_FX_TRAIL : GARDEN_FX_ALL));
                garden_fx_set_flags(&fx,flags);
                garden_log_printf("[FX_CTRL] flags=%d flash=%d trail=%d rings=%d\r\n",flags,
                    !!(flags&GARDEN_FX_FLASH),!!(flags&GARDEN_FX_TRAIL),!!(flags&GARDEN_FX_RING));
                break;
            }
            default: continue;
        }
        if (bit) { direction=bit; direction_until=clock+ticks_per_second/3U; }
        hud_dirty[0]=hud_dirty[1]=1;
        last_input=c; input_report_pending=1;
        garden_log_printf("[INPUT] key=%c\r\n",c);
    }
    if (clock>=direction_until) direction=0;
}
/* Call only between normal frames. The Game and its presentation observers
 * stay untouched while the pure test owns the two framebuffer surfaces. */
static uint32_t pure_game_hash(void)
{
    const volatile uint8_t *p=(const volatile uint8_t *)&game;
    uint32_t h=2166136261U;
    for(unsigned i=0;i<sizeof(game);i++)h=(h^p[i])*16777619U;
    return h;
}
static void restore_game_surfaces(void)
{
    if(garden_render_surface_reset()!=GPU_OK) fail("pure surface reset");
    garden_stage_ui_reset(&stage);
    hud_dirty[0]=hud_dirty[1]=score_hud_dirty[0]=score_hud_dirty[1]=1U;
    score_hud_previous=UINT32_MAX;
    for(unsigned i=0;i<2U;i++) {
        render_begin();
        if(gpu_fill(display_back_addr(),DISPLAY_WIDTH,DISPLAY_HEIGHT,
                    DISPLAY_STRIDE_BYTES,0U)!=GPU_OK) fail("pure restore clear");
        unsigned count=make_draws();
        if(g9_route_prepare(draws,count,0U)!=0) fail("pure restore route");
        if(garden_render_frame(draws,count)!=GPU_OK) fail("pure restore frame");
        unsigned sel=display_back_sel();
        if(!hud() || !score_hud()) fail("pure restore hud");
        if(garden_stage_draw(&stage,&game,sel)<0) fail("pure restore modal");
        if(render_end(SWAP_TIMEOUT)!=GPU_OK) fail("pure restore swap");
    }
    g9_route_clear();
    hud_dirty[0]=hud_dirty[1]=score_hud_dirty[0]=score_hud_dirty[1]=1U;
}

int main(void)
{
    bsp_init(); csr_write(mtvec,trap_entry);
    garden_log_bind(log_tx_free,log_tx_put);
    if(stage9_link_init()!=0)fail("Stage9 identity/drain");
    g9_memory_guard_init();g9_memory_reset();
    if(g9_route_init()!=0)fail("Stage9 tile library");
    Stage9LinkStats g9boot;stage9_link_snapshot(&g9boot);
    g9_prev_shown=g9boot.displayed;g9_prev_engine=g9boot.engine;g9_prev_px=g9boot.missing_px;g9_prev_lines=g9boot.missing_lines;g9_prev_reject=g9boot.rejected;g9_prev_skip=g9boot.skipped;
    garden_log_printf("[G9_INIT] pool=%u boss_budget=%u draw_max=%u tile_crc=0x%x initial_shown=%u initial_engine=%u initial_miss_px=%u initial_miss_lines=%u initial_reject=%u initial_skip=%u collision_unchanged=1 tiles_lossless=1 key=K workload_keys=0..5\r\n",GARDEN_MAX_ENEMY_BULLETS,GARDEN_BOSS_BULLET_BUDGET,GARDEN_DRAW_MAX,0x1250b2a5U,g9_prev_shown,g9_prev_engine,g9_prev_px,g9_prev_lines,g9_prev_reject,g9_prev_skip);
    display_debug_init();
    garden_log_printf("\r\nGARDEN Ti60 cropped port 1.1 / upstream 1.0.9 / GPL3+\r\n");
    display_debug_report("FIRST_CPU_READ",0U);
    garden_log_printf("GPU_ID=0x%x expected=0x%x FB_STATUS=0x%x\r\n",GPU_ID,GPU_MAGIC_ID,GPU_FB_STATUS);
    if (GPU_ID!=GPU_MAGIC_ID) fail("GPU identity");
    if (!GARDEN_ATLAS_BYTES || GARDEN_ATLAS_BYTES>ASSET_SIZE_BYTES-8192U) fail("atlas size");
    data_cache_invalidate_all();
    uint32_t crc=atlas_crc();
    garden_log_printf("[ATLAS] bytes=%d crc32=0x%x expected=0x%x\r\n",GARDEN_ATLAS_BYTES,crc,GARDEN_ATLAS_CRC32);
    if (crc!=GARDEN_ATLAS_CRC32) fail("atlas invalid");
    display_init();
    if (display_probe()!=GPU_OK) fail("swap handshake");
    calibrate();
    garden_render_init();
    int alpha_rc=gpu_alpha_verify();
    const GpuAlphaVerifyStats *alpha_probe=gpu_alpha_verify_stats();
    garden_log_printf("[ALPHA_PROBE] rc=%d present=%d cases=%d commands=%d pixels=%d key_pixels=%d pixel_errors=%d source_errors=%d guard_errors=%d\r\n",alpha_rc,alpha_probe->register_present,alpha_probe->cases,alpha_probe->commands,alpha_probe->pixels,alpha_probe->key_pixels,alpha_probe->pixel_errors,alpha_probe->source_errors,alpha_probe->guard_errors);
    if (alpha_rc!=GPU_OK && alpha_rc!=GPU_ESTATE) {
        garden_log_printf("[ALPHA_PROBE_FAIL] first_addr=0x%x expected=0x%x actual=0x%x status=0x%x restore_skipped=%d\r\n",alpha_probe->first_addr,(uint32_t)alpha_probe->first_expected,(uint32_t)alpha_probe->first_actual,alpha_probe->exit_status,alpha_probe->restore_skipped);
    }
    if (alpha_rc!=GPU_OK && !(alpha_rc==GPU_ESTATE && !alpha_probe->register_present && !alpha_probe->commands)) fail("alpha pixels");
    garden_render_alpha_support(alpha_rc==GPU_OK);
    garden_render_batch_mode(0);
    int precision_rc=gpu_precision_verify();
    const GpuPrecisionVerifyStats *precision_probe=gpu_precision_verify_stats();
    garden_log_printf("[PRECISION_PROBE] rc=%d sprites=%u cases=%u commands=%u pixels=%u key_pixels=%u high_region_sources=%u opaque_cases=%u alpha_cases=%u pixel_errors=%u guard_errors=%u source_errors=%u min_source=0x%x max_source=0x%x restore_skipped=%u\r\n",
        precision_rc,precision_probe->sprites,precision_probe->cases,precision_probe->commands,
        precision_probe->pixels,precision_probe->key_pixels,precision_probe->high_region_sources,
        precision_probe->opaque_cases,precision_probe->alpha_cases,precision_probe->pixel_errors,
        precision_probe->guard_errors,precision_probe->source_errors,precision_probe->min_source,
        precision_probe->max_source,precision_probe->restore_skipped);
    if (precision_rc!=GPU_OK) fail("precision pixels");
    data_cache_invalidate_all();
    uint32_t precision_crc=atlas_crc();
    garden_log_printf("[ATLAS_POST_PROBE] bytes=%u crc32=0x%x expected=0x%x asset_end=0x%x panel_base=0x%x probe_base=0x%x probe_end=0x%x\r\n",
        GARDEN_ATLAS_BYTES,precision_crc,GARDEN_ATLAS_CRC32,ASSET_BASE_ADDR+ASSET_SIZE_BYTES,
        ASSET_BASE_ADDR+ASSET_SIZE_BYTES-8192U,GPU_ALPHA_VERIFY_BASE,GPU_ALPHA_VERIFY_END);
    if (precision_crc!=GARDEN_ATLAS_CRC32) fail("atlas changed");
    garden_log_printf("[ALPHA] supported=%d enabled=%d range=0..32 formula=rounded_rgb565\r\n",garden_render_alpha_supported(),garden_render_alpha_enabled());
    /* Human connection/load timing enters the timer value. This is gameplay
     * pseudorandom seeding, not a claim of physical entropy/security. The
     * actual seed is logged; garden_init(seed) remains deterministic. */
    uint64_t seed_time=now64();
    uint32_t initial_seed=GARDEN_FIXED_RUN_SEED ? GARDEN_FIXED_RUN_SEED :
        (uint32_t)seed_time^(uint32_t)(seed_time>>32)^GARDEN_ATLAS_CRC32;
    garden_clear_reset_stats();
    garden_clear_set_yield(clear_yield);
    garden_init(&game,initial_seed);
    garden_motion_reset(&motion);
    garden_motion_observe(&motion,&game);
    garden_fx_reset(&fx);
    /* Keep the animated engine flame; omit whole-plane ghost copies by
     * default so banking/fire no longer reads as a residual second plane. */
    garden_fx_set_flags(&fx,GARDEN_FX_FLASH|GARDEN_FX_RING);
    garden_fx_observe(&fx,&game);
    garden_stage_reset(&stage);
    garden_stage_observe(&stage,&game);
    garden_feedback_reset(&feedback);
    garden_feedback_observe(&feedback,&game);
    garden_scene_reset(&scene);
    garden_scene_observe(&scene,&game);
    garden_actor_ui_reset(&actor_ui);
    garden_actor_ui_observe(&actor_ui,&game);
    garden_animation_reset(&animation);
    garden_animation_observe(&animation,&game);
    garden_combat_reset(&combat);
    garden_combat_observe(&combat,&game);
    render_begin();
    if (garden_render_key_probe()!=GPU_OK) fail("Color Key pixels");
    garden_log_printf("[COLOR_KEY] source pixels and transparent holes verified PASS\r\n");
    /* Initialize both back buffers before measuring steady-state frames. */
    for (unsigned i=0;i<2;i++) {
        render_begin();
        if (garden_render_frame(draws,make_draws())!=GPU_OK) fail("render");
        if (garden_stage_draw(&stage,&game,display_back_sel())<0) fail("banner");
        if (render_end(SWAP_TIMEOUT)!=GPU_OK) fail("swap");
    }
    if(pad_hw_init((uint32_t)(now64()/(BSP_CLINT_HZ/1000U)))!=0)fail("U6RX identity");
    garden_session_reset(&session,&game,(uint32_t)(now64()/(BSP_CLINT_HZ/1000U)),pad_hw_stats()->activity_serial);
    garden_log_printf("[JOYCON_CTRL] GPIO6=115200 A=throw B=3boss X=T Y=CPU/GPU L/R=focus plus=pause minus=restart idle_auto_ms=10000 gameover_idle_ms=15000 edge_triggered=1\r\n");
    garden_log_set_async(async_logs);
    display_stats_reset();
    display_debug_sample(); display_debug_report("GAME_START",0U);
    uint64_t started=now64(), previous=started, accumulator=0;
    uint64_t interval_start=started, total_render=0, total_logic=0;
    uint64_t interval_render=0,interval_logic=0;
    uint64_t cost_build=0,cost_present=0,cost_debug=0,cost_log=0;
    uint32_t peak_logic=0,peak_build=0,peak_work=0,over_period=0;
    uint32_t frame=0,steps=0,interval_frames=0,interval_steps=0;
    uint32_t worst_render=0,last_mode=99,last_attack=99;
    unsigned stability_reported=0,wave_wait_reported=0;
    uint32_t observed_seed=game.initial_seed,round_number=1U,last_charge_window=0U;
    garden_log_printf("[BATCH_CTRL] enabled=0 list=0x0067d000 capacity=256 fill_limit=256 blit_limit=256 first_item_deferred=1 order=consecutive_only toggle=J\r\n");
    garden_log_printf("[READY] COM11 WASD Q F P R T U G I C B V N J\r\n");
    garden_log_printf("[PURE_CTRL] T=toggle v512..1024 c8..30 count=91*per_ray\r\n");
    garden_log_printf("[ENGINEERING_UI] layout=motion_engineering always_visible=1\r\n");
    garden_log_printf("[REWARD_CTRL] bombs=2 bomb_grace_ticks=0 graze_points=5 core_points=25 bullet_budget=80 logical_hz=40 diagnostics_default=1 high_score=RAM_session\r\n");
    garden_log_printf("[ORDNANCE_CTRL] thrown=1 bomb_damage=600 blast_radius_px=90 heal_lives=1 max_hp=10 laser_warn_ticks=40 laser_fire_ticks=40 laser_fade_ticks=12 bullet_path_collision=1 clear_enemy_bullets=0\r\n");
    garden_log_printf("[HAZARD_CTRL] arm_ticks=%u hitbox_half_px=9 pivot=white_core render_clock=shared_player_and_bullets forming=small_dim grace_preserved=1\r\n",GARDEN_BULLET_ARM_TICKS);
    garden_log_printf("[WAVE_CTRL] earliest_boss_tick=1000 gate=all_planned_enemies_killed leftovers_cleared=0 natural_exit_preserved=0 strict_kill_all=1 scheduled=10\r\n");
    garden_log_printf("[ART_CTRL] materials=13 bullet_styles=9 enemy_projectiles_all_new=1 sprite_count=%u atlas_bytes=%u player_bank=1 actor_hp=above damage_numbers=1 laser_left_px=%d laser_right_px=%d laser_y_px=%d\r\n",
        (unsigned)GARDEN_ASSET_COUNT,(unsigned)GARDEN_ATLAS_BYTES,
        GARDEN_BOSS_MUZZLE_LEFT_X_PX,GARDEN_BOSS_MUZZLE_RIGHT_X_PX,GARDEN_BOSS_MUZZLE_Y_PX);
    garden_log_printf("[VOLLEY_CTRL] enemy_one_style=1 boss_train_one_style=1 style_scope=volley enemy_scope=type\r\n");
    garden_log_printf("[RANDOM_CTRL] enabled=1 initial_source=%s seed=0x%x boss_shuffle=3 repeat_adjacent=0 timing_bounded=1 warning_preserved=1\r\n",
        GARDEN_FIXED_RUN_SEED ? "fixed_override" : "CLINT_mix",game.initial_seed);
    garden_log_printf("[RUN_SEED] round=%u seed=0x%x tick=%u\r\n",round_number,game.initial_seed,game.tick);
    clear_report("START",round_number);
    while(1) {
        if(pure_test_requested || boss_test_requested) {
            unsigned three_boss=boss_test_requested;
            pure_test_requested=boss_test_requested=0U;
            pending_events=0U;
            uint64_t test_begin=now64();
            uint32_t paused_tick=game.tick;
            if(g9_started && stage9_link_stop()!=0) fail("pure enter drain");
            g9_started=0U;
            uint32_t saved_game_hash=pure_game_hash();
            int test_result=three_boss ? pure_bullet_test_boss_run():pure_bullet_test_run();
            if(test_result<0) fail("pure test");
            if(game.tick!=paused_tick || pure_game_hash()!=saved_game_hash) fail("pure changed game");
            if(stage9_link_init()!=0) fail("pure return init");
            restore_game_surfaces();
            /* Keep press history across T, but discard game-only actions that
             * occurred while the game was suspended. A held X cannot reenter. */
            pad_hw_poll((uint32_t)(now64()/(BSP_CLINT_HZ/1000U)));
            (void)pad_hw_take_events((uint32_t)(now64()/(BSP_CLINT_HZ/1000U)));
            Stage9LinkStats fresh;stage9_link_snapshot(&fresh);
            g9_prev_shown=fresh.displayed;g9_prev_engine=fresh.engine;
            g9_prev_px=fresh.missing_px;g9_prev_lines=fresh.missing_lines;
            g9_prev_reject=fresh.rejected;g9_prev_skip=fresh.skipped;
            uint64_t resumed=now64(),test_span=resumed-test_begin;
            garden_session_reset(&session,&game,(uint32_t)(resumed/(BSP_CLINT_HZ/1000U)),pad_hw_stats()->activity_serial);
            if(test_result==2)pending_events|=GARDEN_INPUT_RESTART;
            started+=test_span;previous=resumed;accumulator=0U;
            if(direction_until)direction_until+=test_span;
            if(bomb_key_until)bomb_key_until+=test_span;
            last_mode=last_attack=99U;
            interval_start=resumed;interval_frames=interval_steps=0U;
            interval_render=interval_logic=0U;
            cost_build=cost_present=cost_debug=cost_log=0U;
            peak_logic=peak_build=peak_work=over_period=0U;
            g9_min_actual=g9_min_migrated=UINT32_MAX;
            g9_max_actual=g9_max_migrated=0U;
            g9_sum_migrated=g9_sum_legacy=g9_sum_desc=0U;
            garden_log_printf("[PURE_GAME_RESUMED] tick=%u mode=%u demo=%u cpu=%u alpha=%u panel=%u game_unchanged=1\r\n",
                game.tick,game.mode,game.demo_mode,garden_render_is_cpu(),
                garden_render_alpha_enabled(),garden_render_alpha_level());
        }
        uint32_t work_start=now32();
        uint64_t clock=now64();accumulator+=clock-previous;previous=clock;
        poll_input(clock);
        uint32_t pad_ms=(uint32_t)(clock/(BSP_CLINT_HZ/1000U));
        pad_hw_poll(pad_ms);
        controller_held=pad_hw_held(pad_ms);
        uint16_t pad_events=pad_hw_take_events(pad_ms);
        if(pad_events&GARDEN_CONTROLLER_CPU){
            garden_render_mode(!garden_render_is_cpu());
            garden_log_printf("[JOYCON_ACTION] Y cpu=%u\r\n",garden_render_is_cpu());
        }
        if(pad_events&GARDEN_CONTROLLER_TEST){
            pure_test_requested=1U;
            garden_log_printf("[JOYCON_ACTION] X enter_T\r\n");
        }
        if(pad_events&GARDEN_CONTROLLER_BOSS){
            boss_test_requested=1U;
            garden_log_printf("[JOYCON_ACTION] B enter_3boss\r\n");
        }
        pending_events|=pad_events&~(GARDEN_CONTROLLER_CPU|GARDEN_CONTROLLER_TEST|GARDEN_CONTROLLER_BOSS);
        const PadHwStats *activity=pad_hw_stats();
        unsigned previous_demo=game.demo_mode;
        uint16_t timed=garden_session_update(&session,&game,pad_ms,activity->activity_serial,activity->last_activity_ms);
        pending_events|=timed;
        if(previous_demo!=game.demo_mode)garden_log_printf("[IDLE_CTRL] demo=%u idle_ms=%u paused=%u\r\n",game.demo_mode,pad_ms-session.last_activity_ms,game.paused);
        if(timed)garden_log_printf("[GAMEOVER_IDLE] elapsed_ms=%u restart=1\r\n",pad_ms-session.last_activity_ms);
        if(pending_events&GARDEN_INPUT_RESTART)pure_test_requested=boss_test_requested=0U;
        unsigned catchup=0U;uint32_t tick=ticks_per_second/GARDEN_LOGIC_HZ;
        while(accumulator>=tick && catchup<8U) {
            uint32_t t=now32();g9_workload_before_step(&game);garden_step(&game,direction|(slow?GARDEN_INPUT_SLOW:0)|controller_held|pending_events);g9_workload_after_step(&game);
            garden_motion_observe(&motion,&game);garden_fx_observe(&fx,&game);
            garden_stage_observe(&stage,&game);garden_feedback_observe(&feedback,&game);
            garden_scene_observe(&scene,&game);garden_combat_observe(&combat,&game);
            garden_actor_ui_observe(&actor_ui,&game);garden_animation_observe(&animation,&game);
            pending_events=0;uint32_t elapsed=now32()-t;total_logic+=elapsed;interval_logic+=elapsed;
            if(elapsed>peak_logic)peak_logic=elapsed;
            steps++;interval_steps++;catchup++;accumulator-=tick;
        }
        if(accumulator>=tick){missed_logic+=(uint32_t)(accumulator/tick);accumulator%=tick;}
        if(input_report_pending && catchup) {
            garden_log_printf("[INPUT_STATE] key=%c tick=%u mode=%u paused=%u demo=%u cpu=%u alpha=%u panel_alpha=%u slow=%u player_x=%d player_y=%d lives=%u bombs=%u\r\n",
                last_input,game.tick,game.mode,game.paused,game.demo_mode,garden_render_is_cpu(),garden_render_alpha_enabled(),garden_render_alpha_level(),slow,game.player.x,game.player.y,game.lives,game.reward.bombs);
            input_report_pending=0;
        }
        if(game.initial_seed!=observed_seed) {
            garden_session_reset(&session,&game,pad_ms,activity->activity_serial);
            observed_seed=game.initial_seed;round_number++;last_charge_window=0U;
            garden_log_printf("[RUN_SEED] round=%u seed=0x%x tick=%u\r\n",round_number,observed_seed,game.tick);
            clear_report("RESTART",round_number);
        }
        if(game.rhythm.charge_windows!=last_charge_window) {
            last_charge_window=game.rhythm.charge_windows;
            garden_log_printf("[PATTERN] round=%u seed=0x%x tick=%u attack=%u state=%u band=%u charge=%u rest=%u gap=%u remaining=%u windows=%u train=%u laser=%u colour=%u\r\n",
                round_number,game.initial_seed,game.tick,game.boss.attack,game.boss.rhythm_state,game.boss.hp_band,
                game.boss.charge_ticks,game.boss.rest_ticks,game.boss.burst_gap,game.boss.burst_left,
                last_charge_window,game.rhythm.transits_completed,game.laser.phase,game.boss.volley_colour);
        }
        if(game.tick<1000U) wave_wait_reported=0U;
        if(game.mode==GARDEN_MODE_WAVES && game.tick>=1000U && game.enemy_count && !wave_wait_reported){
            garden_log_printf("[WAVE_WAIT] tick=%u alive=%u score=%u spawned=%u killed=%u\r\n",game.tick,game.enemy_count,game.score,game.wave_next,game.wave_kills);
            wave_wait_reported=1U;
        }
        if(game.mode!=last_mode || (game.boss.active && game.boss.attack!=last_attack)) {
            garden_log_printf("[GAME] tick=%u mode=%u attack=%u lives=%u score=%u eb=%u peak=%u best=%u enemies=%u spawned=%u killed=%u\r\n",game.tick,game.mode,game.boss.attack,game.lives,game.score,game.enemy_bullet_count,game.enemy_bullet_peak,game.reward.high_score,game.enemy_count,game.wave_next,game.wave_kills);
            if(game.mode!=last_mode) clear_report("MODE",round_number);
            last_mode=game.mode;last_attack=game.boss.attack;hud_dirty[0]=hud_dirty[1]=1U;
        }
        uint32_t visible=garden_stage_visible_score(&stage,&game);
        if(score_hud_previous!=visible){score_hud_previous=visible;score_hud_dirty[0]=score_hud_dirty[1]=1U;}
        if(!game.paused) {
            unsigned fraction=tick<=UINT32_MAX/256U ? (uint32_t)accumulator*256U/tick:(unsigned)(accumulator*256U/tick);
            garden_combat_set_render_fraction(&combat,fraction);garden_scene_set_render_fraction(&scene,fraction);
            garden_animation_set_fraction(&animation,fraction);garden_motion_set_fraction(&motion,fraction);
        }
        uint32_t build_start=now32();
        unsigned count=make_draws();
        if(g9_route_prepare(draws,count,g9_enabled && !garden_render_is_cpu())!=0)fail("Stage9 route");
        render_begin();unsigned sel=display_back_sel();
        if(garden_stage_prepare_frame(&stage,&game,sel)<0)fail("modal clear");
        const G9RouteStats *gr=g9_route_stats();
        if(game.enemy_bullet_count<g9_min_actual)g9_min_actual=game.enemy_bullet_count;
        if(game.enemy_bullet_count>g9_max_actual)g9_max_actual=game.enemy_bullet_count;
        if(gr->migrated<g9_min_migrated)g9_min_migrated=gr->migrated;
        if(gr->migrated>g9_max_migrated)g9_max_migrated=gr->migrated;
        g9_sum_migrated+=gr->migrated;g9_sum_legacy+=gr->legacy;g9_sum_desc+=gr->descriptors;
        uint32_t build_ticks=now32()-build_start;cost_build+=build_ticks;
        if(build_ticks>peak_build)peak_build=build_ticks;
        uint32_t t=now32();if(garden_render_frame(draws,count)!=GPU_OK)fail("frame");
        if(!(frame&7U))hud_dirty[0]=hud_dirty[1]=1U;
        if(hud_dirty[sel])hud_dirty[sel]=!hud();
        if(score_hud_dirty[sel])score_hud_dirty[sel]=!score_hud();
        if(garden_stage_draw(&stage,&game,sel)<0)fail("stage/modal");
        if(garden_render_is_cpu()){asm volatile("fence rw,rw" ::: "memory");data_cache_invalidate_all();}
        uint32_t rendered=now32()-t;
        if(rendered>ticks_per_second/60U){if(garden_render_is_cpu())cpu_over_budget++;else gpu_over_budget++;}
        if(rendered>worst_render)worst_render=rendered;
        total_render+=rendered;interval_render+=rendered;
        uint32_t work_ticks=now32()-work_start;
        if(work_ticks>peak_work)peak_work=work_ticks;
        if(work_ticks>ticks_per_second/60U)over_period++;
        uint32_t phase_start=now32();
        if(g9_enabled || g9_started) {
            const G9RouteStats *r=g9_route_stats();
            if(stage9_link_present(r->atlas_addr,r->desc_addr,r->descriptors,display_back_addr(),sel)!=0)fail("Stage9 handoff");
            g9_started=1U;
        }
        if(render_end(SWAP_TIMEOUT)!=GPU_OK)errors++;
        if(!g9_enabled && g9_started){if(stage9_link_stop()!=0)fail("Stage9 stop");g9_started=0U;}
        if(g9_memory_guard_check())fail("Stage9 DDR guard");
        cost_present+=now32()-phase_start;
        frame++;interval_frames++;phase_start=now32();display_debug_sample();
        cost_debug+=now32()-phase_start;
        uint32_t log_start=now32();
        uint64_t end=now64();
        if(end-interval_start>=ticks_per_second*2ULL) {
            display_debug_report("LIVE",frame);
            Stage9LinkStats g9s;stage9_link_snapshot(&g9s);
            const G9RouteStats *g9r=g9_route_stats();
            uint64_t g9span=end-interval_start;
            garden_log_printf("[G9_PERF] frame=%u target=%u path=%u hz=%u tick_lo=%u tick_hi=%u updates=%u logic_steps=%u shown_delta=%u engine_delta=%u miss_px=%u miss_lines=%u reject=%u skip=%u actual_min=%u actual_max=%u migrated_min=%u migrated_max=%u migrated_sum=%u legacy_sum=%u desc_sum=%u logic_us=%u build_us=%u render_us=%u work_peak_us=%u missed_logic=%u config_max_us=%u guard=0\r\n",
                frame,g9_workload_get_target(),g9_enabled,(unsigned)ticks_per_second,(uint32_t)g9span,(uint32_t)(g9span>>32),interval_frames,interval_steps,
                g9s.displayed-g9_prev_shown,g9s.engine-g9_prev_engine,g9s.missing_px-g9_prev_px,g9s.missing_lines-g9_prev_lines,g9s.rejected-g9_prev_reject,g9s.skipped-g9_prev_skip,
                g9_min_actual,g9_max_actual,g9_min_migrated,g9_max_migrated,(uint32_t)g9_sum_migrated,(uint32_t)g9_sum_legacy,(uint32_t)g9_sum_desc,
                interval_steps?microseconds(interval_logic/interval_steps):0U,interval_frames?microseconds(cost_build/interval_frames):0U,
                interval_frames?microseconds(interval_render/interval_frames):0U,microseconds(peak_work),missed_logic,microseconds(g9s.max_config_ticks));
            g9_prev_shown=g9s.displayed;g9_prev_engine=g9s.engine;g9_prev_px=g9s.missing_px;g9_prev_lines=g9s.missing_lines;g9_prev_reject=g9s.rejected;g9_prev_skip=g9s.skipped;
            g9_min_actual=g9_min_migrated=UINT32_MAX;g9_max_actual=g9_max_migrated=0U;g9_sum_migrated=g9_sum_legacy=g9_sum_desc=0U;

            if(periodic_logs&2U)garden_log_printf("[G9_FRAME] frame=%u target=%u actual=%u enabled=%u migrated=%u legacy=%u desc=%u tiles=%u region=%u alpha_fallback=%u occlusion=%u tile_miss=%u desc_full=%u shown=%u engine=%u miss_px=%u miss_lines=%u skip=%u reject=%u max_line=%u handoffs=%u errors=%u guard=0\r\n",
                frame,g9_workload_get_target(),game.enemy_bullet_count,g9_enabled,g9r->migrated,g9r->legacy,g9r->descriptors,g9r->tiles,g9r->fallback_region,g9r->fallback_alpha,g9r->fallback_occlusion,g9r->fallback_tiles,g9r->fallback_desc,
                g9s.displayed,g9s.engine,g9s.missing_px,g9s.missing_lines,g9s.skipped,g9s.rejected,g9s.max_line,g9s.submissions,g9s.errors);

            interval_fps10=(uint32_t)((uint64_t)interval_frames*ticks_per_second*10U/(end-interval_start));
            interval_render_us=microseconds(interval_render/interval_frames);
            interval_logic_us=interval_steps?microseconds(interval_logic/interval_steps):0U;
            const GardenRenderStats *rs=garden_render_stats();const GardenLogStats *ls=garden_log_stats();
            if(periodic_logs&1U)garden_log_printf("[LIVE] secs=%u frame=%u fps10=%u render_us=%u worst_us=%u backend=%s eb=%u errors=%u swap_timeout=%u front_reject=%u atlas_bounds=%u missed=%u demo=%u alpha=%u device_ms=%u log_dropped=%u log_truncated=%u\r\n",
                (uint32_t)((end-started)/ticks_per_second),frame,interval_fps10,interval_render_us,microseconds(worst_render),garden_render_is_cpu()?"CPU":"GPU",game.enemy_bullet_count,errors+rs->errors+log_errors(),display_stats()->timeouts,display_stats()->front_rejects+rs->bad_targets,rs->atlas_bounds,missed_logic,game.demo_mode,garden_render_alpha_enabled(),(uint32_t)((end-started)*1000ULL/ticks_per_second),ls->dropped_lines,ls->truncated_lines);
            garden_log_pump();
            if(periodic_logs&2U){
            garden_log_printf("[REWARD] tick=%u mode=%u phase=%u bombs=%u used=%u cleared=%u graze=%u cores=%u spawned=%u lost=%u score=%u best=%u run_ticks=%u hits=%u\r\n",game.tick,game.mode,game.boss.hp_band,game.reward.bombs,game.reward.bombs_used,game.reward.bullets_cleared,game.reward.graze_count,game.reward.items_collected,game.reward.items_spawned,game.reward.items_dropped,game.score,game.reward.high_score,game.reward.run_ticks,game.reward.hits_taken);
            garden_log_printf("[ORDNANCE] tick=%u mode=%u launched=%u impacts=%u targets=%u bomb_damage=%u heals_spawned=%u heals_collected=%u heals_restored=%u laser_warnings=%u laser_shots=%u laser_hits=%u swept_hits=%u flight=%u blast=%u laser=%u lives=%u\r\n",
                game.tick,game.mode,game.weapon_stats.bombs_launched,game.weapon_stats.bomb_impacts,
                game.weapon_stats.bomb_targets,game.weapon_stats.bomb_damage,game.weapon_stats.heals_spawned,
                game.weapon_stats.heals_collected,game.weapon_stats.heals_restored,game.weapon_stats.laser_warnings,
                game.weapon_stats.laser_shots,game.weapon_stats.laser_hits,game.weapon_stats.swept_hits,
                game.ordnance.active,game.ordnance.blast_ticks,game.laser.phase,game.lives);
            garden_log_printf("[HAZARD] tick=%u mode=%u armed=%u forming=%u armed_contacts=%u forming_contacts=%u grace=%u respawn=%u player_x=%d player_y=%d lives=%u\r\n",
                game.tick,game.mode,game.hazard.armed,game.hazard.forming,game.hazard.armed_contacts,
                game.hazard.forming_contacts,game.player.grace,game.player.respawning,
                game.player.x,game.player.y,game.lives);
            garden_log_printf("[WAVE] tick=%u mode=%u alive=%u boss=%u spawned=%u killed=%u\r\n",game.tick,game.mode,game.enemy_count,game.boss.active,game.wave_next,game.wave_kills);
            const GardenRenderStats *batch_stats=garden_render_stats();
        garden_log_printf("[BATCH] tick=%u mode=%u enabled=%u submissions=%u batches=%u instances=%u fill=%u blit=%u\r\n",game.tick,game.mode,garden_render_is_batch(),batch_stats->commands,batch_stats->batch_commands,batch_stats->batch_instances,batch_stats->batch_fill_commands,batch_stats->batch_blit_commands);
            }
        garden_log_printf("[FRAME_COST] frames=%u steps=%u logic_avg_us=%u logic_peak_us=%u build_avg_us=%u build_peak_us=%u render_avg_us=%u present_avg_us=%u debug_avg_us=%u log_avg_us=%u work_peak_us=%u work_over_period=%u log_cost_one_window_delayed=1\r\n",
                interval_frames,interval_steps,interval_logic_us,microseconds(peak_logic),
                microseconds(cost_build/interval_frames),microseconds(peak_build),interval_render_us,
                microseconds(cost_present/interval_frames),microseconds(cost_debug/interval_frames),
                microseconds(cost_log/interval_frames),microseconds(peak_work),over_period);
            cost_build=cost_present=cost_debug=cost_log=0U;
            peak_logic=peak_build=peak_work=over_period=0U;
            interval_start=end;interval_frames=interval_steps=0U;interval_render=interval_logic=0U;
        }
        garden_log_pump();
        cost_log+=now32()-log_start;
        (void)stability_reported;(void)total_render;(void)total_logic;
    }
}
