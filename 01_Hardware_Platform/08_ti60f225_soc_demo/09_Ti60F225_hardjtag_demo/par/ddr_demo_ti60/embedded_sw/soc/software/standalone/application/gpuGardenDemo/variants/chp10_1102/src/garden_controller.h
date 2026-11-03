/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GARDEN_CONTROLLER_H
#define GARDEN_CONTROLLER_H

#include <stdint.h>

#define GARDEN_CONTROLLER_TIMEOUT_MS 200U
/* Application actions: deliberately outside GardenInput's game bits. */
#define GARDEN_CONTROLLER_CPU (1U << 9)
#define GARDEN_CONTROLLER_TEST (1U << 10)
#define GARDEN_CONTROLLER_BOSS (1U << 11)

/* One instance per player. No UART, timer, MMIO or game-state dependency.
 * now_ms must be a monotonic uint32_t millisecond counter; wrap is supported.
 * Call only from one execution context, after accepting a complete frame. */
typedef struct {
    uint32_t last_frame_ms;
    uint32_t activity_serial, last_activity_ms;
    uint16_t held;
    uint16_t pending_events;
    uint16_t previous_buttons;
    uint8_t previous_misc;
    int8_t axis_x;
    int8_t axis_y;
    uint8_t connected;
    uint8_t enable_select_toggle;
    uint8_t app_armed; /* X/Y require a release after init or link timeout. */
} GardenController;

/* The legacy init argument is retained for callers; minus always restarts. */
void garden_controller_init(GardenController *controller,
                            unsigned enable_select_toggle);

/* Feed accepted controller states, including neutral/released states.
 * LX/LY: left/up negative. D-pad: up=1, down=2, right=4, left=8.
 * Printed Joy-Con buttons: A=1, B=2, X=4, Y=8, L=0x10, R=0x20.
 * Misc: minus=2 restarts, plus=4 pauses. B enters the three-Boss mode.
 * X toggles T; Y toggles game CPU/GPU. Neutral heartbeat packets do not
 * increment activity_serial; held physical controls and release edges do.
 * At >=200ms silence, expire old state BEFORE deriving the new press edges:
 * game buttons held on reconnection are treated as new presses. X/Y are
 * different: a release must be observed first, preventing a held toggle
 * from changing modes after a timeout. */
void garden_controller_feed(GardenController *controller, uint32_t now_ms,
                            int16_t lx, int16_t ly, uint16_t buttons,
                            uint8_t dpad, uint8_t misc);

/* Expire stale input, then return GardenInput held direction/SLOW bits. */
uint16_t garden_controller_poll(GardenController *controller, uint32_t now_ms);

/* Expire stale input, then return and clear pending GardenInput press events.
 * Events survive release/neutral frames until consumed. Multiple unconsumed
 * presses of the same event coalesce into one bit. Call once per logic step. */
uint16_t garden_controller_take_events(GardenController *controller,
                                       uint32_t now_ms);

#endif
