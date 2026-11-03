/* SPDX-License-Identifier: GPL-3.0-or-later
 * Garden of Coloured Lights 1.0.9, a reduced Ti60 demonstration port.
 * Original game by Linley Henzell. See vendor/garden-1.0.9/COPYING.
 * The port's scope and altered behavior are described in garden_core.c.
 */
#ifndef GARDEN_CORE_H
#define GARDEN_CORE_H

#include <stdint.h>

#define GARDEN_WIDTH 640
#define GARDEN_HEIGHT 480
#define GARDEN_GRAIN 1000
#define GARDEN_ANGLE_FULL 1024
#define GARDEN_LOGIC_HZ 40
#define GARDEN_TICK_MS 25
#define GARDEN_MAX_ENEMIES 8
#define GARDEN_MAX_PLAYER_BULLETS 24
#define GARDEN_MAX_ENEMY_BULLETS 192
#define GARDEN_MAX_EFFECTS 8

enum GardenInput {
    GARDEN_INPUT_LEFT = 1u << 0,
    GARDEN_INPUT_RIGHT = 1u << 1,
    GARDEN_INPUT_UP = 1u << 2,
    GARDEN_INPUT_DOWN = 1u << 3,
    GARDEN_INPUT_SLOW = 1u << 4,
    /* The application supplies these three as one-step press events. */
    GARDEN_INPUT_PAUSE = 1u << 5,
    GARDEN_INPUT_RESTART = 1u << 6,
    GARDEN_INPUT_TOGGLE_DEMO = 1u << 7
};

enum GardenMode {
    GARDEN_MODE_WAVES,
    GARDEN_MODE_BOSS,
    GARDEN_MODE_VICTORY,
    GARDEN_MODE_GAMEOVER
};

enum GardenEnemyType {
    GARDEN_ENEMY_NONE,
    GARDEN_ENEMY_DARTER,
    GARDEN_ENEMY_STOPPER
};

enum GardenBulletType {
    GARDEN_BULLET_NONE,
    GARDEN_BULLET_DART,
    GARDEN_BULLET_DIAMOND
};

typedef struct GardenPlayer {
    int32_t x, y;
    uint16_t grace, respawning, recycle;
    int8_t move_x, move_y;
    uint8_t active;
} GardenPlayer;

typedef struct GardenEnemy {
    int32_t x, y, vx, vy, target_x, target_y;
    int32_t hp;
    int16_t target_time;
    uint16_t angle, aim_angle, recycle, age;
    uint8_t active, type, phase, moves, flash;
} GardenEnemy;

typedef struct GardenPlayerBullet {
    int32_t x, y, vx, vy;
    uint16_t age, damage;
    uint8_t active;
} GardenPlayerBullet;

typedef struct GardenEnemyBullet {
    int32_t x, y, vx, vy;
    /* Original activation cord logic; its decorative line is optional. */
    int32_t cord_x, cord_y, cord_vx, cord_vy;
    uint16_t angle, cord_angle, age, ttl;
    int16_t accel, spin, cord_wait;
    /* colour=1..5 corresponds to the original EBCOL_B1..EBCOL_B5. */
    uint8_t active, type, colour;
} GardenEnemyBullet;

typedef struct GardenBoss {
    int32_t x, y, vx, vy, target_x, target_y;
    int32_t hp, max_hp;
    uint16_t phase_ticks, recycle, move_ticks;
    uint8_t active, attack, status, flash;
} GardenBoss;

typedef struct GardenEffect {
    int32_t x, y;
    uint8_t active, age, big;
} GardenEffect;

typedef struct GardenGame {
    GardenPlayer player;
    GardenEnemy enemies[GARDEN_MAX_ENEMIES];
    GardenPlayerBullet player_bullets[GARDEN_MAX_PLAYER_BULLETS];
    GardenEnemyBullet enemy_bullets[GARDEN_MAX_ENEMY_BULLETS];
    GardenBoss boss;
    GardenEffect effects[GARDEN_MAX_EFFECTS];
    uint32_t tick, rng, initial_seed, score;
    uint32_t dropped_enemy_bullets, dropped_player_bullets;
    uint32_t boss_attack_shots[3];
    uint16_t enemy_bullet_count, player_bullet_count, enemy_count;
    uint16_t enemy_bullet_peak, end_ticks;
    uint8_t lives, mode, paused, demo_mode;
} GardenGame;

/* All x/y and speeds use original 1/1000 pixel units. Angle zero is east,
 * 256 is south. init starts an honest, vulnerable automatic demonstration.
 * Call step at 40 Hz. Rendering can run independently at another frequency.
 * The game owns no heap, OS, driver, asset, Allegro, or floating-point state.
 */
void garden_init(GardenGame *game, uint32_t seed);
void garden_step(GardenGame *game, uint16_t buttons);

#endif
