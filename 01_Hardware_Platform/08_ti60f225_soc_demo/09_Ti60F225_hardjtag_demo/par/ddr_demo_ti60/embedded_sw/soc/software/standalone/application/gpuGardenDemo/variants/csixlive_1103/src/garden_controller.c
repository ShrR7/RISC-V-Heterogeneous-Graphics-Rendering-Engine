/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_controller.h"
#include "garden_core.h"

#include <string.h>

static void expire(GardenController *controller, uint32_t now_ms)
{
    if (controller->connected &&
        (uint32_t)(now_ms - controller->last_frame_ms) >=
            GARDEN_CONTROLLER_TIMEOUT_MS) {
        controller->held = 0U;
        controller->pending_events = 0U;
        controller->previous_buttons = 0U;
        controller->previous_misc = 0U;
        controller->axis_x = 0;
        controller->axis_y = 0;
        controller->connected = 0U;
        controller->app_armed = 0U;
    }
}

static int8_t axis_update(int8_t previous, int16_t value)
{
    if (value <= -160) return -1;
    if (value >= 160) return 1;
    if (previous < 0 && value < -100) return -1;
    if (previous > 0 && value > 100) return 1;
    return 0;
}

void garden_controller_init(GardenController *controller,
                            unsigned enable_select_toggle)
{
    memset(controller, 0, sizeof(*controller));
    controller->enable_select_toggle = (uint8_t)(enable_select_toggle != 0U);
}

void garden_controller_feed(GardenController *controller, uint32_t now_ms,
                            int16_t lx, int16_t ly, uint16_t buttons,
                            uint8_t dpad, uint8_t misc)
{
    uint16_t held = 0U;
    uint16_t pressed;
    uint8_t misc_pressed;

    expire(controller, now_ms);
    pressed = (uint16_t)(buttons & (uint16_t)~controller->previous_buttons);
    misc_pressed = (uint8_t)(misc & (uint8_t)~controller->previous_misc);
    controller->axis_x = axis_update(controller->axis_x, lx);
    controller->axis_y = axis_update(controller->axis_y, ly);

    if (controller->axis_x < 0 || (dpad & 8U)) held |= GARDEN_INPUT_LEFT;
    if (controller->axis_x > 0 || (dpad & 4U)) held |= GARDEN_INPUT_RIGHT;
    if (controller->axis_y < 0 || (dpad & 1U)) held |= GARDEN_INPUT_UP;
    if (controller->axis_y > 0 || (dpad & 2U)) held |= GARDEN_INPUT_DOWN;
    if (buttons & 0x30U) held |= GARDEN_INPUT_SLOW;

    controller->app_armed |= (uint8_t)(~buttons & 0x0cU);
    if (pressed & controller->app_armed & 4U)
        controller->pending_events |= GARDEN_CONTROLLER_TEST;
    if (pressed & controller->app_armed & 8U)
        controller->pending_events |= GARDEN_CONTROLLER_CPU;
    if (pressed & 1U) controller->pending_events |= GARDEN_INPUT_BOMB;
    if (pressed & 2U) controller->pending_events |= GARDEN_INPUT_RESTART;
    if (misc_pressed & 4U) controller->pending_events |= GARDEN_INPUT_PAUSE;
    if (controller->enable_select_toggle && (misc_pressed & 2U))
        controller->pending_events |= GARDEN_INPUT_TOGGLE_DEMO;

    controller->held = held;
    controller->previous_buttons = buttons;
    controller->previous_misc = misc;
    controller->last_frame_ms = now_ms;
    controller->connected = 1U;
}

uint16_t garden_controller_poll(GardenController *controller, uint32_t now_ms)
{
    expire(controller, now_ms);
    return controller->held;
}

uint16_t garden_controller_take_events(GardenController *controller,
                                       uint32_t now_ms)
{
    uint16_t events;
    expire(controller, now_ms);
    events = controller->pending_events;
    controller->pending_events = 0U;
    return events;
}
