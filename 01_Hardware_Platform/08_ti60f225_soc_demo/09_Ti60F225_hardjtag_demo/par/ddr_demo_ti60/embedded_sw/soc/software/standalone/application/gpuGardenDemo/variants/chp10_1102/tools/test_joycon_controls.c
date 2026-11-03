/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host integration checks for the real GPIO6 FIFO parser and Joy-Con mapping.
 * No hardware or bridge firmware is accessed. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"
#include "garden_controller.h"
#include "pad_hw.h"

static uint8_t fifo[PAD_HW_FIFO_BYTES];
static unsigned head, queued, checks, cases;
static uint32_t overflow_count, framing_count;

#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

uint32_t pad_hw_mmio_read(uint32_t offset)
{
    switch (offset) {
    case 0x00U: return 0x55365258U;
    case 0x04U: {
        uint32_t result;
        if (!queued) return 0U;
        result = 0x80000000U | fifo[head];
        head = (head + 1U) % PAD_HW_FIFO_BYTES;
        queued--;
        return result;
    }
    case 0x08U: return queued;
    case 0x0cU: return overflow_count;
    case 0x10U: return framing_count;
    case 0x18U: return 115200U;
    case 0x1cU: return 0x00010000U;
    default: fprintf(stderr,"unexpected read 0x%x\n", offset); exit(1);
    }
}

void pad_hw_mmio_write(uint32_t offset, uint32_t value)
{
    CHECK(offset == 0x14U);
    if (value & 1U) { head = queued = 0U; }
    if (value & 2U) overflow_count = framing_count = 0U;
}

static void push(const uint8_t *data, unsigned count)
{
    CHECK(queued + count <= PAD_HW_FIFO_BYTES);
    for (unsigned i = 0U; i < count; i++) {
        fifo[(head + queued) % PAD_HW_FIFO_BYTES] = data[i];
        queued++;
    }
}

static void build_frame(uint8_t f[30], uint8_t player, uint8_t seq,
                        int16_t lx, int16_t ly, uint16_t buttons,
                        uint8_t dpad, uint8_t misc)
{
    unsigned sum = 0U;
    memset(f, 0, 30U);
    f[0] = 0xaaU; f[1] = 0x55U; f[2] = player; f[3] = seq;
    f[4] = (uint8_t)lx; f[5] = (uint8_t)((uint16_t)lx >> 8);
    f[6] = (uint8_t)ly; f[7] = (uint8_t)((uint16_t)ly >> 8);
    f[12] = (uint8_t)buttons; f[13] = (uint8_t)(buttons >> 8);
    f[14] = dpad; f[15] = misc;
    for (unsigned i = 2U; i < 28U; i++) sum += f[i];
    f[28] = (uint8_t)sum; f[29] = 0x0aU;
}

static void packet(uint32_t ms, uint8_t player, uint8_t seq,
                   int16_t lx, int16_t ly, uint16_t buttons,
                   uint8_t dpad, uint8_t misc)
{
    uint8_t f[30];
    build_frame(f, player, seq, lx, ly, buttons, dpad, misc);
    push(f, sizeof f);
    pad_hw_poll(ms);
    CHECK(queued == 0U);
}

static void fresh(void)
{
    head = queued = 0U;
    overflow_count = framing_count = 0U;
    CHECK(pad_hw_init(0U) == 0);
    CHECK(pad_hw_stats()->present && pad_hw_stats()->enabled);
}

static void done(const char *label)
{ printf("PASS %s\n", label); cases++; }

static void test_mapping(void)
{
    fresh();
    packet(0U, 1U, 0U, 0, 0, 0U, 0U, 0U);
    CHECK(pad_hw_take_events(0U) == 0U);
    packet(10U, 1U, 1U, 0, 0, 4U, 0U, 0U);
    CHECK(pad_hw_take_events(10U) == GARDEN_CONTROLLER_TEST); /* printed X */
    packet(20U, 1U, 2U, 0, 0, 0U, 0U, 0U);
    packet(30U, 1U, 3U, 0, 0, 8U, 0U, 0U);
    CHECK(pad_hw_take_events(30U) == GARDEN_CONTROLLER_CPU); /* printed Y */
    packet(40U, 1U, 4U, 0, 0, 0U, 0U, 0U);
    packet(50U, 1U, 5U, 0, 0, 3U, 0U, 6U);
    CHECK(pad_hw_take_events(50U) == (GARDEN_INPUT_BOMB |
          GARDEN_CONTROLLER_BOSS | GARDEN_INPUT_RESTART | GARDEN_INPUT_PAUSE));
    packet(60U, 1U, 6U, 0, 0, 0x10U, 0U, 0U);
    CHECK(pad_hw_held(60U) == GARDEN_INPUT_SLOW);
    packet(70U, 1U, 7U, 0, 0, 0x20U, 0U, 0U);
    CHECK(pad_hw_held(70U) == GARDEN_INPUT_SLOW);
    packet(80U, 1U, 8U, 0, 0, 0U, 0U, 0U);
    CHECK(pad_hw_held(80U) == 0U);
    done("printed X=T, Y=CPU/GPU; A/B, L/R, plus and minus mappings");
}

static void test_held_and_simultaneous(void)
{
    fresh();
    packet(0U, 2U, 0U, 0, 0, 0U, 0U, 0U);
    packet(10U, 2U, 1U, 0, 0, 0x0fU, 0U, 6U);
    CHECK(pad_hw_take_events(10U) == (GARDEN_CONTROLLER_CPU |
          GARDEN_CONTROLLER_TEST | GARDEN_INPUT_BOMB | GARDEN_INPUT_RESTART |
          GARDEN_INPUT_PAUSE | GARDEN_CONTROLLER_BOSS));
    for (uint32_t i = 2U; i < 30U; i++) {
        packet(i*10U, 2U, (uint8_t)i, 0, 0, 0x0fU, 0U, 6U);
        CHECK(pad_hw_take_events(i*10U) == 0U);
    }
    packet(300U, 2U, 30U, 0, 0, 0U, 0U, 0U);
    packet(310U, 2U, 31U, 0, 0, 0x0cU, 0U, 0U);
    packet(320U, 2U, 32U, 0, 0, 0U, 0U, 0U);
    CHECK(pad_hw_take_events(320U) == (GARDEN_CONTROLLER_CPU | GARDEN_CONTROLLER_TEST));
    CHECK(pad_hw_take_events(320U) == 0U);
    done("one event per held press; simultaneous and queued edges survive release");
}

static void test_t_boundary(void)
{
    fresh();
    packet(0U, 1U, 0U, 0, 0, 0U, 0U, 0U);
    packet(10U, 1U, 1U, 0, 0, 4U, 0U, 0U); /* printed X */
    CHECK(pad_hw_take_events(10U) == GARDEN_CONTROLLER_TEST); /* enter T */
    packet(20U, 1U, 2U, 0, 0, 4U, 0U, 0U);
    CHECK(pad_hw_take_events(20U) == 0U); /* held does not exit T */
    packet(30U, 1U, 3U, 0, 0, 0U, 0U, 0U);
    packet(40U, 1U, 4U, 0, 0, 0x07U, 0U, 6U); /* A/B/X */
    uint16_t events = pad_hw_take_events(40U);
    CHECK(events & GARDEN_CONTROLLER_TEST); /* exit T */
    /* T consumes the complete word while using only X, so its A/B/+/- are
     * not left queued for the resumed game. No parser reset is performed. */
    CHECK((events & (GARDEN_INPUT_BOMB | GARDEN_INPUT_RESTART |
          GARDEN_INPUT_PAUSE | GARDEN_CONTROLLER_BOSS)) != 0U);
    CHECK(pad_hw_take_events(40U) == 0U);
    packet(50U, 1U, 5U, 0, 0, 0x07U, 0U, 6U);
    CHECK(pad_hw_take_events(50U) == 0U); /* no immediate reenter */
    packet(60U, 1U, 6U, 0, 0, 0U, 0U, 0U);
    packet(70U, 1U, 7U, 0, 0, 4U, 0U, 0U);
    CHECK(pad_hw_take_events(70U) == GARDEN_CONTROLLER_TEST);
    done("T entry/return preserves X edge history and consumes game-only actions");
}

static void test_timeout(void)
{
    fresh();
    packet(0U, 1U, 0U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(0U) == 0U); /* first packet cannot toggle */
    packet(10U, 1U, 1U, 0, 0, 0U, 0U, 0U);
    packet(20U, 1U, 2U, 300, -300, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(20U) == (GARDEN_CONTROLLER_CPU | GARDEN_CONTROLLER_TEST));
    CHECK(pad_hw_held(219U) == (GARDEN_INPUT_RIGHT | GARDEN_INPUT_UP));
    CHECK(pad_hw_held(220U) == 0U);
    packet(230U, 1U, 3U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(230U) == 0U);
    packet(240U, 1U, 4U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(240U) == 0U);
    packet(250U, 1U, 5U, 0, 0, 8U, 0U, 0U); /* release X only */
    packet(260U, 1U, 6U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(260U) == GARDEN_CONTROLLER_TEST);
    packet(270U, 1U, 7U, 0, 0, 4U, 0U, 0U); /* release Y only */
    packet(280U, 1U, 8U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(280U) == GARDEN_CONTROLLER_CPU);
    done("init/200ms timeout requires independent X/Y release before rearming");
}

static void test_players(void)
{
    fresh();
    packet(0U, 3U, 0U, -300, 0, 0U, 0U, 0U);
    CHECK(pad_hw_stats()->active_player == 3U);
    CHECK(pad_hw_held(0U) == GARDEN_INPUT_LEFT);
    packet(10U, 4U, 0U, 300, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_stats()->active_player == 3U);
    CHECK(pad_hw_stats()->other_players == 1U);
    CHECK(pad_hw_take_events(10U) == 0U);
    packet(200U, 4U, 0U, 300, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_stats()->active_player == 4U);
    CHECK(pad_hw_stats()->rebinds == 1U);
    CHECK(pad_hw_take_events(200U) == 0U);
    CHECK(pad_hw_held(200U) == GARDEN_INPUT_RIGHT);
    packet(210U, 4U, 1U, 0, 0, 0U, 0U, 0U);
    packet(220U, 4U, 2U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(pad_hw_take_events(220U) == (GARDEN_CONTROLLER_CPU | GARDEN_CONTROLLER_TEST));
    done("first valid slot binds; other players cannot steal a live binding");
}

static void test_directions_and_parser(void)
{
    static const uint16_t expected[4] = {GARDEN_INPUT_UP, GARDEN_INPUT_DOWN,
                                        GARDEN_INPUT_RIGHT, GARDEN_INPUT_LEFT};
    fresh();
    packet(0U, 1U, 254U, 0, 0, 0U, 0U, 0U);
    for (unsigned i = 0; i < 4U; i++) {
        packet(10U + i*10U, 1U, (uint8_t)(255U+i), 0, 0, 0U,
               (uint8_t)(1U << i), 0U);
        CHECK(pad_hw_held(10U+i*10U) == expected[i]);
    }
    CHECK(pad_hw_stats()->seq_gaps == 0U); /* 255 -> 0 is valid */
    packet(50U, 1U, 3U, -160, 160, 0x10U, 0U, 0U);
    CHECK(pad_hw_held(50U) == (GARDEN_INPUT_LEFT | GARDEN_INPUT_DOWN | GARDEN_INPUT_SLOW));
    packet(60U, 1U, 4U, -101, 101, 0U, 0U, 0U);
    CHECK(pad_hw_held(60U) == (GARDEN_INPUT_LEFT | GARDEN_INPUT_DOWN));
    packet(70U, 1U, 5U, -100, 100, 0U, 0U, 0U);
    CHECK(pad_hw_held(70U) == 0U);
    CHECK(pad_hw_take_events(70U) == 0U);
    uint8_t f[30];
    build_frame(f, 1U, 6U, 0, 0, 0x0cU, 0U, 0U);
    f[28] ^= 1U;
    push(f, sizeof f); pad_hw_poll(80U);
    CHECK(pad_hw_stats()->bad == 1U);
    CHECK(pad_hw_take_events(80U) == 0U);
    build_frame(f, 1U, 6U, 0, 0, 0x0cU, 0U, 0U);
    push(f, 12U); pad_hw_poll(90U);
    CHECK(pad_hw_take_events(90U) == 0U);
    push(f+12U, 18U); pad_hw_poll(100U);
    CHECK(pad_hw_take_events(100U) == (GARDEN_CONTROLLER_CPU | GARDEN_CONTROLLER_TEST));
    CHECK(pad_hw_stats()->bad == 1U);
    CHECK(pad_hw_stats()->overflow == 0U && pad_hw_stats()->framing == 0U);
    done("D-pad/axis neutral hysteresis, sequence wrap, checksum and split packets");
}

static void test_physical_activity(void)
{
    fresh();
    packet(0U,1U,0U,0,0,0U,0U,0U);
    packet(100U,1U,1U,99,-99,0U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==0U);
    packet(150U,1U,2U,300,0,0U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==1U && pad_hw_stats()->last_activity_ms==150U);
    packet(180U,1U,3U,300,0,0U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==2U && pad_hw_stats()->last_activity_ms==180U);
    packet(200U,1U,4U,0,0,0U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==3U && pad_hw_stats()->last_activity_ms==200U);
    for(unsigned i=0;i<120U;i++)packet(250U+i*50U,1U,(uint8_t)(5U+i),0,0,0U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==3U && pad_hw_stats()->last_activity_ms==200U);
    packet(6300U,1U,125U,0,0,0x80U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==4U); /* even an unmapped physical button */
    packet(6310U,2U,0U,300,0,1U,0U,0U);
    CHECK(pad_hw_stats()->activity_serial==4U); /* unbound controller cannot take over */
    uint8_t f[30];build_frame(f,1U,126U,300,0,1U,0U,0U);f[28]^=1U;
    push(f,sizeof f);pad_hw_poll(6320U);
    CHECK(pad_hw_stats()->activity_serial==4U);
    packet(6600U,2U,1U,0,0,0U,0U,0U); /* rebind preserves global activity counter */
    CHECK(pad_hw_stats()->activity_serial==4U && pad_hw_stats()->last_activity_ms==6300U);
    done("neutral heartbeat/noise, real held/release activity, invalid frames and player rebind");
}

static void test_clock_wrap(void)
{
    GardenController c;
    garden_controller_init(&c, 1U);
    garden_controller_feed(&c, UINT32_MAX-50U, -300, 0, 0U, 0U, 0U);
    garden_controller_feed(&c, UINT32_MAX-40U, -300, 0, 0x0cU, 0U, 0U);
    CHECK(garden_controller_take_events(&c, UINT32_MAX-40U) ==
          (GARDEN_CONTROLLER_CPU | GARDEN_CONTROLLER_TEST));
    CHECK(garden_controller_poll(&c, 158U) == GARDEN_INPUT_LEFT);
    CHECK(garden_controller_poll(&c, 159U) == 0U);
    garden_controller_feed(&c, 160U, 0, 0, 0x0cU, 0U, 0U);
    CHECK(garden_controller_take_events(&c, 160U) == 0U);
    done("uint32 millisecond wrap keeps timeout and held toggle protection");
}

int main(void)
{
    test_mapping();
    test_held_and_simultaneous();
    test_t_boundary();
    test_timeout();
    test_players();
    test_directions_and_parser();
    test_physical_activity();
    test_clock_wrap();
    printf("PASS Joy-Con controls: %u cases, %u assertions; real controller+FIFO parser, host MMIO only\n", cases, checks);
    return 0;
}
