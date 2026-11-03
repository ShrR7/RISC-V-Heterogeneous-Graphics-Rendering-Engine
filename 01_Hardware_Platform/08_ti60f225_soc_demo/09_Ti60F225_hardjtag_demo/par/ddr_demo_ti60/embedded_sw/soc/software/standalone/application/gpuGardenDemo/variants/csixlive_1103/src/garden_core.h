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
#define GARDEN_WAVE_ENEMIES 10U
#define GARDEN_MAX_PLAYER_BULLETS 24
#define GARDEN_MAX_ENEMY_BULLETS 2000
/* Normal gameplay scans/admission retain the accepted192-slot extent.
 * Only explicitly selected injected stress widens this up to the physical
 * pool. garden_init restores192,including every ordinary restart. */
extern unsigned garden_enemy_scan_limit;
/* Independent Stage9 game candidate: CPU state lives in guarded DDR.
 * Physical capacity is2000; keep the accepted80-bullet Boss gameplay budget
 * until a separately labelled dynamic capacity experiment raises admission.
 */
#define GARDEN_BOSS_BULLET_BUDGET 80U
#define GARDEN_MAX_EFFECTS 8
#define GARDEN_MAX_PICKUPS 8
#define GARDEN_END_TICKS 320U
#define GARDEN_DEATH_TICKS 100U
#define GARDEN_GRAZE_SCORE 5U
#define GARDEN_PICKUP_SCORE 25U
#define GARDEN_BOMB_DAMAGE 600U
#define GARDEN_BOMB_RADIUS 90000
#define GARDEN_BOMB_BLAST_TICKS 16U
#define GARDEN_LASER_WARN_TICKS 40U
#define GARDEN_LASER_FIRE_TICKS 40U
#define GARDEN_LASER_FADE_TICKS 12U
#define GARDEN_LASER_HALF_WIDTH 6000
/* User cruiser128x132: body anchor(64,66), last opaque gun row85.
 * Pixel-local left/right muzzles(22,85)/(107,85) are deliberately asymmetric.
 * Gameplay rails, gun pods, warning, fire and debris share these coordinates. */
#define GARDEN_BOSS_MUZZLE_LEFT_X_PX (-42)
#define GARDEN_BOSS_MUZZLE_RIGHT_X_PX 43
#define GARDEN_BOSS_MUZZLE_Y_PX 19
#define GARDEN_BOSS_CORE_Y_PX (-19)
#define GARDEN_BOSS_NOSE_Y_PX 60
#define GARDEN_BULLET_ARM_TICKS 6U

enum GardenPickupKind { GARDEN_PICKUP_CORE, GARDEN_PICKUP_HEAL };
enum GardenLaserPhase { GARDEN_LASER_OFF, GARDEN_LASER_WARN,
                        GARDEN_LASER_FIRE, GARDEN_LASER_FADE };

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
    /* Legacy cord position/velocity storage was write-only after the
     * age-based arming fix and is removed here (16B per bullet saved).
     * Heading and cord_wait diagnostics remain; danger is age>=6. */
    uint16_t angle, cord_angle, age, ttl;
    int16_t accel, spin, cord_wait;
    /* colour=1..5 is EBCOL_B1..5; grazed bit0 is lifetime graze, bit1 marks
     * a newly armed endpoint this step (no retroactive movement collision). */
    uint8_t active, type, colour, grazed;
} GardenEnemyBullet;

/* One authoritative danger state for collision, AI, graze and presentation.
 * age0..5 is a150ms forming cue; age6 onward is a normal dangerous bullet.
 * cord_wait mirrors the remaining forming time/-1 for diagnostic readers.
 */
static inline unsigned garden_enemy_bullet_is_armed(const GardenEnemyBullet *b)
{return b && b->active && b->age>=GARDEN_BULLET_ARM_TICKS;}

typedef struct GardenBoss {
    int32_t x, y, vx, vy, target_x, target_y;
    int32_t hp, max_hp;
    uint16_t phase_ticks, recycle, move_ticks;
    uint8_t active, attack, status, flash;
    uint16_t rhythm_ticks, charge_ticks, rest_ticks;
    uint8_t rhythm_state, burst_left, burst_gap, hp_band;
    /* Remaining attack bits1/2/4 in the current three-item shuffled bag. */
    uint8_t attack_bag;
    /* Appearance bank1..5, fixed across a complete attack train and WAIT.
     * Uses prior tail padding: GardenBoss and GardenGame sizes unchanged. */
    uint8_t volley_colour;
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
 * original20/80/1000kill rewards remain. Storage is bounded by the24consumed
 * player shots and all9possible targets of the single physical blast.
 */
/* Each consumed player shot contributes at most one event; one bomb can
 * damage all8enemies and the Boss once. No merging of independent hits.
 */
#define GARDEN_COMBAT_EVENT_MAX (GARDEN_MAX_PLAYER_BULLETS+GARDEN_MAX_ENEMIES+1)
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
    uint8_t active,magnet,kind;
} GardenPickup;

typedef struct GardenBomb {
    int32_t x,y;
    uint16_t age;
    uint8_t active,blast_ticks;
} GardenBomb;
typedef struct GardenLaser {
    /* World millipixels. Locked throughout warning/fire/fade. Visible core
     * includes both edges x+-6000 (13integer pixels), y..480000. Collision
     * intersects that core with the original player's18x18pixel hit square.
     */
    int32_t x[2],y;
    uint16_t ticks;
    uint8_t phase,cycle;
} GardenLaser;
typedef struct GardenWeaponStats {
    uint32_t bombs_launched,bomb_impacts,bomb_targets,bomb_damage;
    uint32_t heals_spawned,heals_collected,heals_restored;
    uint32_t laser_warnings,laser_shots,laser_hits,swept_hits;
} GardenWeaponStats;

typedef struct GardenHazardStats {
    uint16_t armed,forming;
    /* Per-run bullet-tick point contacts inside the original9px square,
     * sampled before damage, only when player has no grace/respawn. Several
     * simultaneous armed contacts still cause the normal single life loss.
     */
    uint32_t forming_contacts,armed_contacts;
} GardenHazardStats;

typedef struct GardenRewardStats {
    /* Per-run counters; high_score alone survives manual/automatic restart.
     * run_ticks stops at the final hit, excluding the 8s end presentation.
     */
    uint32_t high_score,run_ticks,hits_taken,graze_count,items_collected;
    uint32_t graze_score_total,item_score_total,items_spawned,items_dropped;
    uint32_t bombs_used,bullets_cleared;
    int32_t pickup_flash_x,pickup_flash_y;
    uint8_t bombs,pickup_flash_ticks,bomb_flash_ticks,pickup_count,pickup_flash_kind;
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
    /* Per-step combat events and cumulative rhythm ledger. */
    GardenCombatEvents combat;
    GardenRhythmStats rhythm;
    /* Temporary monotone allocation state, enabled only inside one Boss
     * generator call. Both fields are zero at every public step boundary.
     */
    uint16_t boss_alloc_cursor;
    uint8_t boss_alloc_enabled;
    GardenPickup pickups[GARDEN_MAX_PICKUPS];
    GardenRewardStats reward;
    GardenBomb ordnance;
    GardenLaser laser;
    GardenWeaponStats weapon_stats;
    GardenHazardStats hazard;
    /* Successful scheduled deliveries and actual wave kills, reset per run. */
    uint8_t wave_next,wave_kills;
} GardenGame;

/* All x/y and speeds use original 1/1000 pixel units. Angle zero is east,
 * 256 is south. init starts an honest, vulnerable automatic demonstration.
 * Call step at 40 Hz. Rendering can run independently at another frequency.
 * A nonzero init seed reproduces the complete game and restart sequence.
 * Every restart advances initial_seed using the nonzero xorshift successor;
 * the application supplies a platform timer seed only for the first run.
 * Public init clears arbitrary input storage. Internal restart clears the
 * prior admitted bullet extent (at least192); extent changes must go through
 * public init, as g9_workload_set does, rather than shrinking the scan global
 * while leaving live upper slots behind.
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
