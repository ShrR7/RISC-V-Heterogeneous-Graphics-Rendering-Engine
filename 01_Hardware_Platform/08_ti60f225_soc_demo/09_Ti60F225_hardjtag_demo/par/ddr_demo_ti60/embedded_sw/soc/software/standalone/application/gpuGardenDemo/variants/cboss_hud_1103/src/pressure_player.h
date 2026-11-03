/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
/* Independent pressure-scene pilot: no Game writes, collision, skills or FX.
 * Reserve DDR [0x009c0000,0x009c4000) for two64x64 RGB565 background caches.
 * Call init after pressure backdrops are prepared; it rejects sprites>64x64.
 * Each writable-buffer frame MUST restore BEFORE any clear or scene draw,
 * then draw AFTER all legacy bullets and BEFORE Stage9 present. Both return
 * GPU_OK or the original GPU error. Never reset while a cached pilot is still
 * visible unless the caller will repaint both complete scene backgrounds. */
int pressure_player_init(void);
int pressure_player_restore(uint32_t framebuffer,unsigned buffer);
int pressure_player_draw(uint32_t framebuffer,unsigned buffer,uint32_t now_ms);
