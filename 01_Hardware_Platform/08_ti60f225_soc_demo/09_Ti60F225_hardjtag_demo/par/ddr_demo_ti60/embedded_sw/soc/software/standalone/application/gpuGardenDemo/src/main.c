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
#include "gpu_display.h"
#include "display_debug.h"
#include "garden_core.h"
#include "garden_assets.h"
#include "garden_render.h"

#define SWAP_TIMEOUT 20000000U
#define SEED 12345U
#define BENCH_FRAMES 32U
static GardenGame game;
static GardenDraw draws[GARDEN_DRAW_MAX];
static uint32_t ticks_per_second;
static uint32_t gpu_bench_us, cpu_bench_us;
static uint32_t interval_fps10, interval_logic_us, interval_render_us;
static uint32_t errors, missed_logic;
static uint32_t gpu_over_budget,cpu_over_budget;
static uint16_t pending_events, direction;
static uint64_t direction_until;
static unsigned slow, bench_requested;
static unsigned hud_dirty[2]={1,1};

void trap_entry(void);
void trap(void)
{
    bsp_printf("\r\n[TRAP] mcause=0x%x mepc=0x%x mtval=0x%x\r\n",(uint32_t)csr_read(mcause),(uint32_t)csr_read(mepc),(uint32_t)csr_read(mtval));
    while (1) { }
}
static uint32_t now32(void) { return clint_getTimeLow(BSP_CLINT); }
static uint64_t now64(void) { return clint_getTime(BSP_CLINT); }
static uint32_t microseconds(uint64_t t) { return (uint32_t)(t*1000000ULL/ticks_per_second); }
static void fail(const char *why)
{
    bsp_printf("RESULT: GARDEN FAIL %s\r\n",why);
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
    uint32_t t=now32();
    for (unsigned i=0;i<60;i++) {
        active^=1U; display_request_fb(active);
        if (display_wait_swap(active,SWAP_TIMEOUT)!=GPU_OK) fail("swap calibration");
    }
    ticks_per_second=now32()-t;
    if (ticks_per_second<1000000U) fail("invalid timer calibration");
    bsp_printf("[CAL] ticks_per_second=%d logic_hz=%d display_hz=60\r\n",ticks_per_second,GARDEN_LOGIC_HZ);
}
static unsigned make_draws(void)
{
    unsigned n=0;
    /* Draw the actors first, then shots. The solid hit point is never enlarged. */
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) if (game.enemies[i].active) {
        const GardenEnemy *e=&game.enemies[i];
        draws[n++]=(GardenDraw){e->x/GARDEN_GRAIN,e->y/GARDEN_GRAIN,e->type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1:GARDEN_ASSET_STOPPER1,32U};
    }
    if (game.boss.active) draws[n++]=(GardenDraw){game.boss.x/GARDEN_GRAIN,game.boss.y/GARDEN_GRAIN,GARDEN_ASSET_BOSS1,32U};
    if (game.player.active && (!game.player.grace || (game.tick&3U)<2U)) draws[n++]=(GardenDraw){game.player.x/GARDEN_GRAIN,game.player.y/GARDEN_GRAIN,GARDEN_ASSET_PLAYER,32U};
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) if (game.player_bullets[i].active) {
        const GardenPlayerBullet *b=&game.player_bullets[i];
        draws[n++]=(GardenDraw){b->x/GARDEN_GRAIN,b->y/GARDEN_GRAIN,GARDEN_ASSET_SHOT,32U};
    }
    for (unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) if (game.enemy_bullets[i].active) {
        const GardenEnemyBullet *b=&game.enemy_bullets[i];
        draws[n++]=(GardenDraw){b->x/GARDEN_GRAIN,b->y/GARDEN_GRAIN,garden_bullet_id(b->type,b->colour,b->angle),32U};
    }
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) if (game.effects[i].active) {
        const GardenEffect *e=&game.effects[i];
        unsigned frame=e->age/5U; if (frame>3U) frame=3U;
        draws[n++]=(GardenDraw){e->x/GARDEN_GRAIN,e->y/GARDEN_GRAIN,GARDEN_ASSET_EXPLOSION0+frame,(uint8_t)(e->age<24U ? 24U-e->age : 0U)};
    }
    return n;
}
static const char *mode_name(void)
{
    if (game.paused) return "PAUSED";
    switch (game.mode) {
        case GARDEN_MODE_WAVES: return "STAGE 1 WAVES";
        case GARDEN_MODE_BOSS: return "STAGE 1 BOSS";
        case GARDEN_MODE_VICTORY: return "VICTORY / R TO PLAY";
        default: return "GAME OVER / R TO PLAY";
    }
}
static char *decimal(char *out,uint32_t value,unsigned digits)
{
    char reversed[10]; unsigned n=0;
    do { reversed[n++]=(char)('0'+value%10U); value/=10U; } while (value);
    while (n<digits) reversed[n++]='0';
    while (n) *out++=reversed[--n];
    *out=0; return out;
}
static void hud_number(int y,const char *label,uint32_t value)
{
    char text[40];
    strcpy(text,label);
    char *out=text+strlen(text); *out++=' ';
    decimal(out,value,1);
    garden_render_text(72,y,text);
}
static void hud(void)
{
    char text[44];
    garden_render_box(68,176,224,360,0);
    garden_render_text(72,176,garden_render_is_cpu()?"RENDER: RISC-V CPU":"RENDER: FPGA GPU");
    garden_render_text(72,200,game.demo_mode?"CONTROL: AUTO DEMO":"CONTROL: COM11 UART");
    garden_render_text(72,224,mode_name());
    hud_number(248,"SCORE",game.score);
    hud_number(272,"LIVES",game.lives);
    hud_number(296,"ENEMY BULLETS",game.enemy_bullet_count);
    hud_number(320,"PEAK BULLETS",game.enemy_bullet_peak);
    hud_number(344,"RENDER US",interval_render_us);
    strcpy(text,"FPS ");
    char *out=decimal(text+4,interval_fps10/10U,1); *out++='.';
    decimal(out,interval_fps10%10U,1);
    garden_render_text(72,368,text);
    hud_number(392,"LOGIC US",interval_logic_us);
    hud_number(416,"GPU BENCH US",gpu_bench_us);
    hud_number(440,"CPU BENCH US",cpu_bench_us);
    if (gpu_bench_us) {
        uint32_t ratio=(uint32_t)((uint64_t)cpu_bench_us*100U/gpu_bench_us);
        strcpy(text,"SPEEDUP "); out=decimal(text+8,ratio/100U,1); *out++='.';
        out=decimal(out,ratio%100U,2); *out++='X'; *out=0;
        garden_render_text(72,464,text);
    }
    hud_number(488,"ERRORS",errors+garden_render_stats()->errors);
    garden_render_box(68,576,232,40,0);
    garden_render_text(72,576,!garden_render_alpha_supported()?"ALPHA: WAIT BITSTREAM":(garden_render_alpha_enabled()?"ALPHA: ON":"ALPHA: OFF"));
    if (garden_render_alpha_supported()) hud_number(600,"PANEL ALPHA / 32",garden_render_alpha_level());
    if (game.boss.active) {
        garden_render_box(1000,400,216,64,0);
        hud_number(512,"BOSS ATTACK",game.boss.attack);
        garden_render_text(1000,400,"BOSS HP");
        garden_render_box(1000,424,200,12,0x18A3);
        if (game.boss.max_hp>0) {
            int width=(int)((uint64_t)(game.boss.hp>0?game.boss.hp:0)*200U/game.boss.max_hp);
            garden_render_box(1000,424,width,12,0xF9AE);
        }
    } else garden_render_box(1000,400,216,64,0);
    garden_render_text(1000,500,"SOURCE: GARDEN 1.0.9");
    garden_render_text(1000,524,"GPL V3 OR LATER");
    /* CPU HUD writes share the same publication requirement as sprites. */
    if (garden_render_is_cpu()) { asm volatile("fence rw,rw" ::: "memory"); data_cache_invalidate_all(); }
}
static void poll_input(uint64_t clock)
{
    while (uart_readOccupancy(BSP_UART_TERMINAL)) {
        char c=uart_read(BSP_UART_TERMINAL);
        if (c>='A' && c<='Z') c+=(char)('a'-'A');
        uint16_t bit=0;
        switch (c) {
            case 'a': bit=GARDEN_INPUT_LEFT; break;
            case 'd': bit=GARDEN_INPUT_RIGHT; break;
            case 'w': bit=GARDEN_INPUT_UP; break;
            case 's': bit=GARDEN_INPUT_DOWN; break;
            case 'f': slow^=1U; break;
            case 'p': pending_events^=GARDEN_INPUT_PAUSE; break;
            case 'r': case ' ': case '\r': pending_events|=GARDEN_INPUT_RESTART; break;
            case 't': pending_events^=GARDEN_INPUT_TOGGLE_DEMO; break;
            case 'c': garden_render_mode(!garden_render_is_cpu()); break;
            case 'b': bench_requested=1; break;
            case 'v':
                if (garden_render_alpha_toggle()!=GPU_OK) bsp_printf("[ALPHA] unavailable: waiting for compatible bitstream\r\n");
                else bsp_printf("[ALPHA] enabled=%d\r\n",garden_render_alpha_enabled());
                break;
            case 'n': garden_render_alpha_cycle(); break;
            default: continue;
        }
        if (bit) { direction=bit; direction_until=clock+ticks_per_second/3U; }
        hud_dirty[0]=hud_dirty[1]=1;
        bsp_printf("[INPUT] key=%c\r\n",c);
    }
    if (clock>=direction_until) direction=0;
}
static void benchmark(void)
{
    const unsigned count=128;
    unsigned saved_cpu=garden_render_is_cpu();
    uint32_t results[2];
    for (unsigned i=0;i<count;i++) {
        draws[i]=(GardenDraw){24+(int)(i%16U)*39,36+(int)(i/16U)*54,garden_bullet_id(GARDEN_BULLET_DART,1U+i%5U,(i%8U)*128U),(uint8_t)(garden_render_alpha_enabled()?16U:32U)};
    }
    bsp_printf("[BENCH] frozen scene 128 sprites; same erase, RGB565, color key and alpha; alpha=%d panel_alpha=%d\r\n",garden_render_alpha_enabled(),garden_render_alpha_level());
    for (unsigned cpu=0;cpu<2;cpu++) {
        uint64_t total=0,elapsed_start=now64();
        garden_render_mode(cpu);
        for (unsigned i=0;i<BENCH_FRAMES+2U;i++) {
            render_begin();
            uint32_t t=now32();
            if (garden_render_frame(draws,count)!=GPU_OK) errors++;
            if (i>=2U) total+=(uint32_t)(now32()-t);
            if (render_end(SWAP_TIMEOUT)!=GPU_OK) errors++;
        }
        results[cpu]=microseconds(total/BENCH_FRAMES);
        uint32_t fps10=(uint32_t)((uint64_t)(BENCH_FRAMES+2U)*ticks_per_second*10U/(now64()-elapsed_start));
        bsp_printf("[BENCH] backend=%s sprites=%d render_us=%d presented_fps10=%d errors=%d\r\n",cpu?"CPU":"GPU",count,results[cpu],fps10,errors);
    }
    gpu_bench_us=results[0]; cpu_bench_us=results[1];
    garden_render_mode(saved_cpu);
    hud_dirty[0]=hud_dirty[1]=1;
}

int main(void)
{
    bsp_init(); csr_write(mtvec,trap_entry);
    display_debug_init();
    bsp_printf("\r\nGARDEN Ti60 cropped port 1.1 / upstream 1.0.9 / GPL3+\r\n");
    display_debug_report("FIRST_CPU_READ",0U);
    bsp_printf("GPU_ID=0x%x expected=0x%x FB_STATUS=0x%x\r\n",GPU_ID,GPU_MAGIC_ID,GPU_FB_STATUS);
    if (GPU_ID!=GPU_MAGIC_ID) fail("GPU identity");
    if (!GARDEN_ATLAS_BYTES || GARDEN_ATLAS_BYTES>ASSET_SIZE_BYTES-8192U) fail("atlas size");
    data_cache_invalidate_all();
    uint32_t crc=atlas_crc();
    bsp_printf("[ATLAS] bytes=%d crc32=0x%x expected=0x%x\r\n",GARDEN_ATLAS_BYTES,crc,GARDEN_ATLAS_CRC32);
    if (crc!=GARDEN_ATLAS_CRC32) fail("atlas not loaded or damaged");
    display_init();
    if (display_probe()!=GPU_OK) fail("Stage6 swap handshake missing");
    calibrate();
    garden_render_init();
    int alpha_rc=gpu_alpha_verify();
    const GpuAlphaVerifyStats *alpha_probe=gpu_alpha_verify_stats();
    bsp_printf("[ALPHA_PROBE] rc=%d present=%d cases=%d commands=%d pixels=%d key_pixels=%d pixel_errors=%d source_errors=%d guard_errors=%d\r\n",alpha_rc,alpha_probe->register_present,alpha_probe->cases,alpha_probe->commands,alpha_probe->pixels,alpha_probe->key_pixels,alpha_probe->pixel_errors,alpha_probe->source_errors,alpha_probe->guard_errors);
    if (alpha_rc!=GPU_OK && alpha_rc!=GPU_ESTATE) {
        bsp_printf("[ALPHA_PROBE_FAIL] first_addr=0x%x expected=0x%x actual=0x%x status=0x%x restore_skipped=%d\r\n",alpha_probe->first_addr,(uint32_t)alpha_probe->first_expected,(uint32_t)alpha_probe->first_actual,alpha_probe->exit_status,alpha_probe->restore_skipped);
    }
    if (alpha_rc!=GPU_OK && !(alpha_rc==GPU_ESTATE && !alpha_probe->register_present && !alpha_probe->commands)) fail("Alpha pixel verification");
    garden_render_alpha_support(alpha_rc==GPU_OK);
    bsp_printf("[ALPHA] supported=%d enabled=%d range=0..32 formula=rounded_rgb565\r\n",garden_render_alpha_supported(),garden_render_alpha_enabled());
    garden_init(&game,SEED);
    render_begin();
    if (garden_render_key_probe()!=GPU_OK) fail("Color Key pixel verification");
    bsp_printf("[COLOR_KEY] source pixels and transparent holes verified PASS\r\n");
    /* Initialize both back buffers before measuring steady-state frames. */
    for (unsigned i=0;i<2;i++) {
        render_begin();
        if (garden_render_frame(draws,make_draws())!=GPU_OK) fail("initial rendering");
        if (render_end(SWAP_TIMEOUT)!=GPU_OK) fail("initial swap");
    }
    benchmark();
    display_stats_reset();
    display_debug_sample(); display_debug_report("GAME_START",0U);
    uint64_t started=now64(), previous=started, accumulator=0;
    uint64_t interval_start=started, total_render=0, total_logic=0;
    uint64_t interval_render=0,interval_logic=0;
    uint32_t frame=0,steps=0,interval_frames=0,interval_steps=0;
    uint32_t worst_render=0,last_mode=99,last_attack=99;
    unsigned stability_reported=0;
    bsp_printf("[READY] COM11 115200 8N1. WASD move F slow P pause R restart T demo C backend B benchmark V alpha N level\r\n");
    while (1) {
        uint64_t clock=now64();
        accumulator+=clock-previous; previous=clock;
        poll_input(clock);
        if (bench_requested) {
            uint64_t bench_start=now64();
            benchmark(); bench_requested=0;
            /* Freeze gameplay during the explicit comparison. It is separately timed. */
            previous=now64(); accumulator=0;
            uint64_t duration=previous-bench_start;
            started+=duration; interval_start+=duration;
        }
        unsigned catchup=0;
        uint32_t tick=ticks_per_second/GARDEN_LOGIC_HZ;
        while (accumulator>=tick && catchup<8U) {
            uint32_t t=now32();
            garden_step(&game,direction | (slow?GARDEN_INPUT_SLOW:0) | pending_events);
            pending_events=0;
            uint32_t elapsed=now32()-t;
            total_logic+=elapsed; interval_logic+=elapsed;
            steps++; interval_steps++; catchup++; accumulator-=tick;
        }
        if (accumulator>=tick) { missed_logic+=(uint32_t)(accumulator/tick); accumulator%=tick; }
        if (game.mode!=last_mode || (game.boss.active && game.boss.attack!=last_attack)) {
            bsp_printf("[GAME] tick=%d mode=%d attack=%d lives=%d score=%d eb=%d peak=%d\r\n",game.tick,game.mode,game.boss.attack,game.lives,game.score,game.enemy_bullet_count,game.enemy_bullet_peak);
            last_mode=game.mode; last_attack=game.boss.attack;
            hud_dirty[0]=hud_dirty[1]=1;
        }
        unsigned count=make_draws();
        render_begin();
        uint32_t t=now32();
        if (garden_render_frame(draws,count)!=GPU_OK) errors++;
        unsigned sel=display_back_sel();
        if (hud_dirty[sel]) { hud(); hud_dirty[sel]=0; }
        uint32_t rendered=now32()-t;
        if (rendered>ticks_per_second/60U) {
            if (garden_render_is_cpu()) cpu_over_budget++; else gpu_over_budget++;
        }
        if (rendered>worst_render) worst_render=rendered;
        total_render+=rendered; interval_render+=rendered;
        if (render_end(SWAP_TIMEOUT)!=GPU_OK) errors++;
        frame++; interval_frames++;
        display_debug_sample();
        uint64_t end=now64();
        if (end-interval_start>=ticks_per_second*2ULL) {
            interval_fps10=(uint32_t)((uint64_t)interval_frames*ticks_per_second*10U/(end-interval_start));
            interval_render_us=microseconds(interval_render/interval_frames);
            interval_logic_us=interval_steps?microseconds(interval_logic/interval_steps):0;
            const GardenRenderStats *r=garden_render_stats();
            bsp_printf("[LIVE] secs=%d frame=%d fps10=%d logic_us=%d render_us=%d worst_us=%d backend=%s sprites=%d eb=%d pb=%d dropped=%d errors=%d swap_timeout=%d front_reject=%d atlas_bounds=%d missed=%d demo=%d alpha_supported=%d alpha=%d alpha_sprites=%d alpha_commands=%d\r\n",(uint32_t)((end-started)/ticks_per_second),frame,interval_fps10,interval_logic_us,interval_render_us,microseconds(worst_render),garden_render_is_cpu()?"CPU":"GPU",r->sprites,game.enemy_bullet_count,game.player_bullet_count,game.dropped_enemy_bullets,errors+r->errors,display_stats()->timeouts,display_stats()->front_rejects+r->bad_targets,r->atlas_bounds,missed_logic,game.demo_mode,garden_render_alpha_supported(),garden_render_alpha_enabled(),r->alpha_sprites,r->alpha_commands);
            interval_start=end; interval_frames=interval_steps=0; interval_render=interval_logic=0;
            display_debug_report("GARDEN",frame);
            hud_dirty[0]=hud_dirty[1]=1;
        }
        if (!stability_reported && end-started>=600ULL*ticks_per_second) {
            const GardenRenderStats *r=garden_render_stats();
            uint32_t fps10=(uint32_t)((uint64_t)frame*ticks_per_second*10U/(end-started));
            uint32_t faults=errors+r->errors+r->bad_targets+r->atlas_bounds+display_stats()->timeouts+missed_logic;
            bsp_printf("RESULT: GARDEN 600S %s frames=%d fps10=%d logic_steps=%d avg_render_us=%d worst_us=%d errors=%d missed=%d gpu_over_budget=%d cpu_over_budget=%d alpha_supported=%d alpha=%d alpha_commands=%d\r\n",faults?"FAIL":"PASS",frame,fps10,steps,microseconds(total_render/frame),microseconds(worst_render),faults,missed_logic,gpu_over_budget,cpu_over_budget,garden_render_alpha_supported(),garden_render_alpha_enabled(),r->alpha_commands);
            stability_reported=1;
        }
    }
}
