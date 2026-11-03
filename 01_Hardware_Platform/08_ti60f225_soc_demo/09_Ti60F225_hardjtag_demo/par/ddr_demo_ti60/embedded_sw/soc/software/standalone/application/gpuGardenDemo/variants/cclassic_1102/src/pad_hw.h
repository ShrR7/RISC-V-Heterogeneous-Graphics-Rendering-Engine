/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PAD_HW_H
#define PAD_HW_H
#include <stdint.h>

#define PAD_HW_BASE 0xF8100900U
#define PAD_HW_FIFO_BYTES 256U

typedef struct {
    uint32_t frames, bytes, bad, seq_gaps, discarded, fifo_peak;
    uint32_t max_poll_gap_ms, max_frame_gap_ms;
    uint32_t overflow, framing;
    uint32_t startup_count, startup_overflow, startup_framing;
    uint32_t other_players, duplicates;
    int16_t lx, ly;
    uint16_t buttons;
    uint8_t dpad, misc, seq;
    uint8_t present, enabled, active_player;
    uint32_t rebinds, input_age_ms;
    uint32_t activity_serial, last_activity_ms;
} PadHwStats;

/* Call after slow startup checks, immediately before READY. Records startup
 * errors before CTRL=3. No DATA read or CONTROL write if ID/baud/revision fails.
 * First valid player1..4 binds the single-player input and seeds held state
 * without generating press edges. A different player may bind only after
 * >=200ms without a packet from the current binding. */
int pad_hw_init(uint32_t now_ms);
/* Read a COUNT snapshot, then at most 256 32-bit DATA words. Empty/zero byte
 * are distinguished by bit31. Errors remain visible and are never read-clear. */
void pad_hw_poll(uint32_t now_ms);
uint16_t pad_hw_held(uint32_t now_ms);
uint16_t pad_hw_take_events(uint32_t now_ms);
/* Both transitions flush FIFO only, reset parser/edges/sequence anchors, and
 * drop the binding and prime the next packet. While disabled, poll drains bytes into discarded.
 * Counters survive transitions; T duration is excluded from frame gap. */
void pad_hw_mode(unsigned enabled, uint32_t now_ms);
const PadHwStats *pad_hw_stats(void);

#ifdef PAD_HW_HOST_TEST
/* Offsets relative to PAD_HW_BASE. Test code supplies a FIFO/MMIO model. */
uint32_t pad_hw_mmio_read(uint32_t offset);
void pad_hw_mmio_write(uint32_t offset, uint32_t value);
#endif
#endif
