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
/* Gameplay admission limit for Boss pulses. The real192slot pool remains
 * unchanged; whole pulse trains wait for this board rendering budget.
 */
#define GARDEN_BOSS_BULLET_BUDGET 80U
#define GARDEN_MAX_EFFECTS 8
#define GARDEN_MAX_PICKUPS 8
#define GARDEN_END_TICKS 320U
#define GARDEN_DEATH_TICKS 100U
#define GARDEN_GRAZE_SCORE 5U
#define GARDEN_PICKUP_SCORE 25U

enum GardenInput {
    GARDEN_INPUT_LEFT = 1u << 0,
    GARDEN_INPUT_RIGHT = 1u << 1,
    GARDEN_INPUT_UP = 1u << 2,
    GARDEN_INPUT_DOWN = 1u << 3,
    GARDEN_INPUT_SLOW = 1u << 4,
    /* The application supplies these three as one-step press events. */
    GARDEN_INPUT_PAUSE = 1u << 5,
    GARDEN_INPUT_RESTART = 1u << 6,
    GARDEN_INPUT_TOGGLE_DEMO = 1u << 7,
    /* One-step press event, never a held-fire input. */
    GARDEN_INPUT_BOMB = 1u << 8
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

/* A true gameplay schedule: anticipation, finite pulse train, then recovery.
 * WAIT is capacity backpressure, never an unannounced emission. A recovered
 * WAIT starts a fresh complete charge before continuing its pulse train.
 */
enum GardenBossRhythmState {
    GARDEN_BOSS_TRANSIT,
    GARDEN_BOSS_CHARGE,
    GARDEN_BOSS_BURST,
    GARDEN_BOSS_REST,
    GARDEN_BOSS_WAIT
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
    uint8_t active, type, colour, grazed;
} GardenEnemyBullet;

typedef struct GardenBoss {
    int32_t x, y, vx, vy, target_x, target_y;
    int32_t hp, max_hp;
    uint16_t phase_ticks, recycle, move_ticks;
    uint8_t active, attack, status, flash;
    uint16_t rhythm_ticks, charge_ticks, rest_ticks;
    uint8_t rhythm_state, burst_left, burst_gap, hp_band;
} GardenBoss;

typedef struct GardenRhythmStats {
    uint32_t charge_windows, bursts_completed, rest_windows, transits_completed;
    uint32_t capacity_wait_ticks, shots_admitted, shots_deferred, hp_band_changes;
} GardenRhythmStats;

typedef struct GardenEffect {
    int32_t x, y;
    uint8_t active, age, big;
} GardenEffect;

/* Explicit one-step combat events for presentation and score auditing.
 * This edition retains +1 per positive HP-damage hit and changes Boss rhythm;
 * original20/80/1000kill rewards remain. Storage is fixed to the projectile
 * pool: a consumed player projectile can generate at most one event/step.
 */
#define GARDEN_COMBAT_EVENT_MAX GARDEN_MAX_PLAYER_BULLETS
typedef struct GardenHitEvent {
    int32_t x,y,target_x,target_y;
    uint32_t score_delta,kill_reward; /* actually credited / nominal kill award */
    uint16_t damage; /* effective HP damage, excludes overkill */
    uint8_t target_slot,target_type,killed,boss;
} GardenHitEvent;
typedef struct GardenCombatEvents {
    uint32_t tick,hits_total,kills_total,hit_score_total,kill_score_total;
    uint32_t emitted_total,dropped_total;
    uint8_t count;
    GardenHitEvent items[GARDEN_COMBAT_EVENT_MAX];
} GardenCombatEvents;

/* A genuine fixed-capacity gameplay pickup, in the same 1/1000-pixel units.
 * A short ballistic pop precedes the falling/magnetic approach. Magnetic
 * movement never grants credit until the object reaches the player.
 */
typedef struct GardenPickup {
    int32_t x,y,vx,vy;
    uint16_t age;
    uint8_t active,magnet;
} GardenPickup;

typedef struct GardenRewardStats {
    /* Per-run counters; high_score alone survives manual/automatic restart.
     * run_ticks stops at the final hit, excluding the 8s end presentation.
     */
    uint32_t high_score,run_ticks,hits_taken,graze_count,items_collected;
    uint32_t graze_score_total,item_score_total,items_spawned,items_dropped;
    uint32_t bombs_used,bullets_cleared;
    int32_t pickup_flash_x,pickup_flash_y;
    uint8_t bombs,pickup_flash_ticks,bomb_flash_ticks,pickup_count;
} GardenRewardStats;

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
    /* Appended after the original prefix for independent legacy-state proof. */
    GardenCombatEvents combat;
    GardenRhythmStats rhythm;
    /* Temporary monotone allocation state, enabled only inside one Boss
     * generator call. Both fields are zero at every public step boundary.
     */
    uint16_t boss_alloc_cursor;
    uint8_t boss_alloc_enabled;
    GardenPickup pickups[GARDEN_MAX_PICKUPS];
    GardenRewardStats reward;
} GardenGame;

/* All x/y and speeds use original 1/1000 pixel units. Angle zero is east,
 * 256 is south. init starts an honest, vulnerable automatic demonstration.
 * Call step at 40 Hz. Rendering can run independently at another frequency.
 * The game owns no heap, OS, driver, asset, Allegro, or floating-point state.
 */
void garden_init(GardenGame *game, uint32_t seed);
void garden_step(GardenGame *game, uint16_t buttons);
/* Exact number of future unpaused steps to the next charge-ending emission,
 * for reachable Boss states with no competing wave enemies. Returns zero
 * outside the final20 charge ticks, during a burst/rest/transit, or when
 * capacity cannot admit the entire remaining train. Observer is read-only.
 */
unsigned garden_boss_fire_countdown(const GardenGame *game);

#endif
