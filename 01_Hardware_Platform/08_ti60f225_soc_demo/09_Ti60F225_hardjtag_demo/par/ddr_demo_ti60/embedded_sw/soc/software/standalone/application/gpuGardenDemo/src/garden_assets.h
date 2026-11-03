/* SPDX-License-Identifier: GPL-3.0-or-later
 * Generated RGB565 asset metadata; atlas bytes are loaded separately by GDB.
 * Run tools/build_atlas.py to regenerate this file and garden_assets.c.
 */
#ifndef GARDEN_ASSETS_H
#define GARDEN_ASSETS_H
#include <stdint.h>

#define GARDEN_ATLAS_ADDRESS 0x005C4000u
#define GARDEN_KEY_COLOR 0xF81Fu
#define GARDEN_ATLAS_BYTES 160020u
#define GARDEN_ATLAS_CRC32 0xF1E82337u
#define GARDEN_BULLET_ROTATIONS 8u
#define GARDEN_BULLET_COLOURS 5u
#define GARDEN_FONT_WIDTH 8u
#define GARDEN_FONT_HEIGHT 8u
#define GARDEN_FONT_GLYPHS 43u

typedef struct {
    uint32_t offset;                 /* byte offset from GARDEN_ATLAS_ADDRESS */
    uint16_t width, height, stride;   /* stride in bytes (width * 2) */
    int16_t anchor_x, anchor_y;      /* draw top-left at x-anchor_x,y-anchor_y */
} garden_sprite_t;

enum garden_asset_id {
    GARDEN_ASSET_PLAYER = 0,
    GARDEN_ASSET_DARTER1,
    GARDEN_ASSET_STOPPER1,
    GARDEN_ASSET_BOSS1,
    GARDEN_ASSET_SHOT,
    GARDEN_ASSET_EXPLOSION0,
    GARDEN_ASSET_EXPLOSION1,
    GARDEN_ASSET_EXPLOSION2,
    GARDEN_ASSET_EXPLOSION3,
    GARDEN_ASSET_CIRCLE_RED,
    GARDEN_ASSET_CIRCLE_BLUE,
    GARDEN_ASSET_CIRCLE_GREEN,
    GARDEN_ASSET_DART_BASE,
    GARDEN_ASSET_DIAMOND_BASE = GARDEN_ASSET_DART_BASE + 40,
    GARDEN_ASSET_FONT_WHITE_BASE = GARDEN_ASSET_DIAMOND_BASE + 40,
    GARDEN_ASSET_FONT_CYAN_BASE = GARDEN_ASSET_FONT_WHITE_BASE + 43,
    GARDEN_ASSET_COUNT = GARDEN_ASSET_FONT_CYAN_BASE + 43
};

/* Compatibility aliases; bullet helpers preserve all five original colours. */
#define GARDEN_SPR_PLAYER GARDEN_ASSET_PLAYER
#define GARDEN_SPR_ENEMY GARDEN_ASSET_DARTER1
#define GARDEN_SPR_DARTER1 GARDEN_ASSET_DARTER1
#define GARDEN_SPR_STOPPER1 GARDEN_ASSET_STOPPER1
#define GARDEN_SPR_BOSS GARDEN_ASSET_BOSS1
#define GARDEN_SPR_SHOT GARDEN_ASSET_SHOT
#define GARDEN_SPR_EXPLOSION0 GARDEN_ASSET_EXPLOSION0
#define GARDEN_SPR_EXPLOSION1 GARDEN_ASSET_EXPLOSION1
#define GARDEN_SPR_EXPLOSION2 GARDEN_ASSET_EXPLOSION2
#define GARDEN_SPR_EXPLOSION3 GARDEN_ASSET_EXPLOSION3
#define GARDEN_SPR_BULLET_RED GARDEN_ASSET_CIRCLE_RED
#define GARDEN_SPR_BULLET_BLUE GARDEN_ASSET_CIRCLE_BLUE
#define GARDEN_SPR_BULLET_GREEN GARDEN_ASSET_CIRCLE_GREEN

const garden_sprite_t *garden_asset(unsigned id);
/* type 1=DART,2=DIAMOND; colour 1..5=original EBCOL_B1..B5; angle 0..1023. */
unsigned garden_bullet_id(unsigned type, unsigned colour, unsigned angle);
const garden_sprite_t *garden_bullet_asset(unsigned type, unsigned colour, unsigned angle);
/* Uppercase ASCII, space and : / - . _ %; returns white glyph asset ID or -1. */
int garden_glyph(char c);
const garden_sprite_t *garden_glyph_asset(char c, unsigned cyan);
extern const char garden_atlas_sha256[65];
#endif
