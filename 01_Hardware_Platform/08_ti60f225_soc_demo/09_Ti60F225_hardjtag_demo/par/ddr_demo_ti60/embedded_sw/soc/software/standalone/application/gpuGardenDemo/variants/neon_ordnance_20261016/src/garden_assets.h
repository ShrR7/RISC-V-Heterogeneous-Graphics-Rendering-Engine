/* SPDX-License-Identifier: GPL-3.0-or-later
 * Generated RGB565 asset metadata; atlas bytes are loaded separately by GDB.
 * Run tools/build_ordnance_atlas.py to regenerate this file and garden_assets.c.
 */
#ifndef GARDEN_ASSETS_H
#define GARDEN_ASSETS_H
#include <stdint.h>

#define GARDEN_ATLAS_ADDRESS 0x005C4000u
#define GARDEN_KEY_COLOR 0xF81Fu
#define GARDEN_ATLAS_BYTES 710028u
#define GARDEN_ATLAS_CRC32 0x2570AE7Cu
#define GARDEN_BULLET_ROTATIONS 8u
#define GARDEN_PRECISION_ROTATIONS 64u
#define GARDEN_BULLET_COLOURS 5u
#define GARDEN_FONT_WIDTH 8u
#define GARDEN_FONT_HEIGHT 8u
#define GARDEN_FONT_GLYPHS 43u

typedef struct {
    uint32_t offset;                 /* byte offset from GARDEN_ATLAS_ADDRESS */
    uint8_t width, height;           /* tight RGB565 source rows */
    int8_t anchor_x, anchor_y;       /* signed logical pivot */
} garden_sprite_t;
_Static_assert(sizeof(garden_sprite_t)==8u,"Sprite descriptor must be 8 bytes");
static inline unsigned garden_sprite_stride(const garden_sprite_t *sprite)
{
    return (unsigned)sprite->width*2u;
}

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
    GARDEN_ASSET_FONT_LARGE_WHITE_BASE = GARDEN_ASSET_FONT_CYAN_BASE + 43,
    GARDEN_ASSET_FONT_LARGE_CYAN_BASE = GARDEN_ASSET_FONT_LARGE_WHITE_BASE + 43,
    GARDEN_ASSET_PLAYER_FRAME1 = GARDEN_ASSET_FONT_LARGE_CYAN_BASE + 43,
    GARDEN_ASSET_PLAYER_FRAME2,
    GARDEN_ASSET_FLASH_DARTER1 = 266,
    GARDEN_ASSET_FLASH_STOPPER1 = 267,
    GARDEN_ASSET_FLASH_BOSS1 = 268,
    GARDEN_ASSET_FX_RING32 = 269,
    GARDEN_ASSET_FX_RING48 = 270,
    GARDEN_ASSET_FOCUS_VULNERABLE = 271,
    GARDEN_ASSET_FOCUS_GRACE = 272,
    GARDEN_ASSET_HIT_SPARK = 273,
    GARDEN_ASSET_BOSS_WING_L = 274,
    GARDEN_ASSET_BOSS_WING_R = 275,
    GARDEN_ASSET_BOSS_CHARGE = 276,
    GARDEN_ASSET_GARDEN_FAR = 277,
    GARDEN_ASSET_GARDEN_STREAK = 278,
    GARDEN_ASSET_BOSS_CORE = 279,
    GARDEN_ASSET_PRECISION_BASE = 280,
    GARDEN_ASSET_ORDNANCE_HIT_BASE = 840,
    GARDEN_ASSET_ORDNANCE_HIT_COUNT = 6,
    GARDEN_ASSET_ORDNANCE_BLAST_BASE = 846,
    GARDEN_ASSET_ORDNANCE_BLAST_COUNT = 8,
    GARDEN_ASSET_ORDNANCE_BOMB_BASE = 854,
    GARDEN_ASSET_ORDNANCE_BOMB_COUNT = 2,
    GARDEN_ASSET_ORDNANCE_HEAL = 856,
    GARDEN_ASSET_ORDNANCE_BEAM_BASE = 857,
    GARDEN_ASSET_ORDNANCE_BEAM_COUNT = 2,
    GARDEN_ASSET_COUNT = 859
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

/* Three precomposed thruster poses; one sprite draw per player frame. */
static inline unsigned garden_player_id(unsigned tick)
{
    unsigned phase=(tick/3u)%3u;
    return phase ? GARDEN_ASSET_PLAYER_FRAME1+phase-1u : GARDEN_ASSET_PLAYER;
}

const garden_sprite_t *garden_asset(unsigned id);
/* type 1=DART,2=DIAMOND; colour 1..5=original EBCOL_B1..B5; angle 0..1023. */
unsigned garden_bullet_id(unsigned type, unsigned colour, unsigned angle);
const garden_sprite_t *garden_bullet_asset(unsigned type, unsigned colour, unsigned angle);
/*64directions, old8IDs retained; round angle to nearest16units. */
unsigned garden_bullet_precision_id(unsigned type,unsigned colour,unsigned angle);
/*Includes only original80and appended560enemy projectile sprites. */
unsigned garden_asset_is_enemy_bullet(unsigned id);
/* Uppercase ASCII, space and : / - . _ %; returns white glyph asset ID or -1. */
int garden_glyph(char c);
const garden_sprite_t *garden_glyph_asset(char c, unsigned cyan);
extern const char garden_atlas_sha256[65];
#endif
