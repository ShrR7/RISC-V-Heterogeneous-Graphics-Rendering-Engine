/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pad_hw.h"
#include "garden_controller.h"
#include <string.h>

#ifndef PAD_HW_HOST_TEST
static uint32_t pad_hw_mmio_read(uint32_t offset)
{ return *(volatile uint32_t *)(uintptr_t)(PAD_HW_BASE + offset); }
static void pad_hw_mmio_write(uint32_t offset, uint32_t value)
{ *(volatile uint32_t *)(uintptr_t)(PAD_HW_BASE + offset) = value; }
#endif

static PadHwStats stats;
static GardenController controller;
static uint8_t frame[30], used, prime, have_seq, last_seq, have_frame;
static uint32_t last_poll, last_frame;

static int16_t rd_i16(const uint8_t *p)
{ return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static void reset_input(uint32_t now_ms)
{
    garden_controller_init(&controller, 1U);
    used = 0U;
    prime = 1U;
    have_seq = 0U;
    have_frame = 0U;
    last_poll = now_ms;
    stats.lx = 0; stats.ly = 0; stats.buttons = 0U;
    stats.dpad = 0U; stats.misc = 0U; stats.seq = 0U;
    stats.active_player = 0U;
    stats.input_age_ms = UINT32_MAX;
}

static void update_age(uint32_t now_ms)
{ stats.input_age_ms = have_frame ? now_ms - last_frame : UINT32_MAX; }

static void accept(uint32_t now_ms)
{
    uint32_t gap = now_ms - last_frame;
    uint8_t seq = frame[3];
    if (have_frame) {
        if (gap > stats.max_frame_gap_ms) stats.max_frame_gap_ms = gap;
        if (gap >= GARDEN_CONTROLLER_TIMEOUT_MS) have_seq = 0U;
    }
    if (have_seq) {
        uint8_t delta = (uint8_t)(seq - last_seq);
        if (delta == 0U) stats.duplicates++;
        else if (delta > 1U) stats.seq_gaps += (uint32_t)delta - 1U;
    }
    last_seq = seq;
    have_seq = 1U;
    last_frame = now_ms;
    have_frame = 1U;
    stats.frames++;
    stats.input_age_ms = 0U;
    stats.lx = rd_i16(frame + 4); stats.ly = rd_i16(frame + 6);
    stats.buttons = (uint16_t)rd_i16(frame + 12);
    stats.dpad = frame[14]; stats.misc = frame[15]; stats.seq = seq;
    if (prime) {
        /* Expiration cannot erase this seed: controller was reset/disconnected.
         * Held axes are active immediately; existing buttons cause no events. */
        controller.previous_buttons = stats.buttons;
        controller.previous_misc = stats.misc;
        prime = 0U;
    }
    uint32_t activity=controller.activity_serial;
    garden_controller_feed(&controller, now_ms, stats.lx, stats.ly,
                           stats.buttons, stats.dpad, stats.misc);
    if(controller.activity_serial!=activity){
        stats.activity_serial++;stats.last_activity_ms=controller.last_activity_ms;
    }
}

static void resync(void)
{
    /* Only after a full invalid candidate: retain a possible embedded header.
     * This software parser runs on buffered bytes, never at UART bit timing. */
    for (unsigned i = 1U; i < 29U; i++) {
        if (frame[i] == 0xaaU && frame[i + 1U] == 0x55U) {
            unsigned remaining = 30U - i;
            memmove(frame, frame + i, remaining);
            used = (uint8_t)remaining;
            return;
        }
    }
    if (frame[29] == 0xaaU) { frame[0] = 0xaaU; used = 1U; }
    else used = 0U;
}

static void feed(uint8_t byte, uint32_t now_ms)
{
    if (used == 0U) {
        if (byte == 0xaaU) { frame[0] = byte; used = 1U; }
        return;
    }
    if (used == 1U) {
        if (byte == 0x55U) { frame[1] = byte; used = 2U; }
        else if (byte != 0xaaU) used = 0U;
        return;
    }
    frame[used++] = byte;
    if (used < 30U) return;
    unsigned sum = 0U;
    for (unsigned i = 2U; i < 28U; i++) sum += frame[i];
    if (frame[29] != 0x0aU || frame[2] < 1U || frame[2] > 4U ||
        (uint8_t)sum != frame[28]) {
        stats.bad++;
        resync();
        return;
    }
    uint8_t player = frame[2];
    if (stats.active_player != player) {
        if (stats.active_player != 0U && have_frame &&
            (uint32_t)(now_ms - last_frame) < GARDEN_CONTROLLER_TIMEOUT_MS) {
            stats.other_players++;
            used = 0U;
            return;
        }
        if (stats.active_player != 0U) stats.rebinds++;
        /* A new physical/bridge slot must never inherit the old player's
         * held state, pending actions, press edges or sequence anchor. */
        reset_input(now_ms);
        stats.active_player = player;
    }
    accept(now_ms);
    used = 0U;
}

int pad_hw_init(uint32_t now_ms)
{
    memset(&stats, 0, sizeof(stats));
    reset_input(now_ms);
    if (pad_hw_mmio_read(0x00U) != 0x55365258U ||
        pad_hw_mmio_read(0x18U) != 115200U ||
        (pad_hw_mmio_read(0x1cU) >> 16) != 1U) return -1;
    stats.startup_count = pad_hw_mmio_read(0x08U);
    stats.startup_overflow = pad_hw_mmio_read(0x0cU);
    stats.startup_framing = pad_hw_mmio_read(0x10U);
    pad_hw_mmio_write(0x14U, 3U);
    stats.overflow = pad_hw_mmio_read(0x0cU);
    stats.framing = pad_hw_mmio_read(0x10U);
    stats.present = 1U;
    stats.enabled = 1U;
    return 0;
}

void pad_hw_poll(uint32_t now_ms)
{
    if (!stats.present) return;
    uint32_t gap = now_ms - last_poll;
    if (gap > stats.max_poll_gap_ms) stats.max_poll_gap_ms = gap;
    last_poll = now_ms;
    uint32_t overflow = pad_hw_mmio_read(0x0cU);
    uint32_t framing = pad_hw_mmio_read(0x10U);
    if (overflow != stats.overflow || framing != stats.framing) used = 0U;
    stats.overflow = overflow;
    stats.framing = framing;
    uint32_t count = pad_hw_mmio_read(0x08U) & 0x1ffU;
    if (count > PAD_HW_FIFO_BYTES) count = PAD_HW_FIFO_BYTES;
    if (count > stats.fifo_peak) stats.fifo_peak = count;
    for (uint32_t i = 0U; i < count; i++) {
        uint32_t sample = pad_hw_mmio_read(0x04U);
        if ((sample & 0x80000000U) == 0U) break;
        stats.bytes++;
        if (stats.enabled) feed((uint8_t)(sample & 0xffU), now_ms);
        else stats.discarded++;
    }
    /* Errors can arrive during the drain; they must stay visible immediately. */
    overflow = pad_hw_mmio_read(0x0cU);
    framing = pad_hw_mmio_read(0x10U);
    if (overflow != stats.overflow || framing != stats.framing) used = 0U;
    stats.overflow = overflow;
    stats.framing = framing;
    (void)garden_controller_poll(&controller, now_ms);
    update_age(now_ms);
}

uint16_t pad_hw_held(uint32_t now_ms)
{ update_age(now_ms); return stats.enabled ? garden_controller_poll(&controller, now_ms) : 0U; }

uint16_t pad_hw_take_events(uint32_t now_ms)
{ update_age(now_ms); return stats.enabled ? garden_controller_take_events(&controller, now_ms) : 0U; }

void pad_hw_mode(unsigned enabled, uint32_t now_ms)
{
    if (stats.present) {
        uint32_t count = pad_hw_mmio_read(0x08U) & 0x1ffU;
        stats.discarded += count > PAD_HW_FIFO_BYTES ? PAD_HW_FIFO_BYTES : count;
        pad_hw_mmio_write(0x14U, 1U);
    }
    reset_input(now_ms);
    stats.enabled = (uint8_t)(stats.present && enabled != 0U);
}

const PadHwStats *pad_hw_stats(void) { return &stats; }
