/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production collision/scoring contract plus independent legacy-core oracle.
 * The legacy translation unit owns its original GardenGame type. Only copied
 * bytes cross the bridge; never call old code with a new GardenGame pointer.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"

extern size_t combat_legacy_size(void);
extern void combat_legacy_init(uint32_t seed);
extern void combat_legacy_load(const void *prefix, size_t size);
extern void combat_legacy_step(uint16_t buttons);
extern void combat_legacy_copy(void *prefix, size_t size);

static unsigned long checks;
static unsigned long compared_steps, observed_hits, observed_kills;
static unsigned long observed_boss_hits, victories, automatic_restarts;
static void check_at(int ok, const char *what, int line)
{
    ++checks;
    if (!ok) {
        fprintf(stderr, "FAIL line=%d check=%s\n", line, what);
        exit(1);
    }
}
#define CHECK(x) check_at(!!(x), #x, __LINE__)

static uint32_t sat_add(uint32_t value, uint32_t amount)
{
    return amount > UINT32_MAX - value ? UINT32_MAX : value + amount;
}

static void compare_prefix(const GardenGame *actual)
{
    GardenGame expected, normalized;
    size_t size = offsetof(GardenGame, combat);
    memset(&expected, 0, sizeof(expected));
    combat_legacy_copy(&expected, size);
    normalized = *actual;
    normalized.score = expected.score;
    if (memcmp(&normalized, &expected, size)) {
        const unsigned char *a = (const unsigned char *)&normalized;
        const unsigned char *b = (const unsigned char *)&expected;
        size_t i;
        for (i = 0; i < size; ++i) {
            if (a[i] != b[i]) {
                fprintf(stderr,
                    "Physics prefix mismatch tick=%u offset=%lu new=%u old=%u\n",
                    actual->tick, (unsigned long)i, a[i], b[i]);
                break;
            }
        }
        CHECK(0);
    }
    CHECK(memcmp(&normalized, &expected, size) == 0);
}

static void pair_load(const GardenGame *g)
{
    combat_legacy_load(g, offsetof(GardenGame, combat));
}

static void pair_step(GardenGame *g, uint16_t input)
{
    GardenGame before = *g;
    GardenGame old_after;
    uint32_t nominal_kills = 0, hits = 0, kills = 0;
    uint32_t credited = 0;
    uint32_t replay_score = before.score;
    uint32_t damage_sum[GARDEN_MAX_ENEMIES + 1] = { 0 };
    uint32_t remaining_hp[GARDEN_MAX_ENEMIES + 1] = { 0 };
    uint8_t used_projectile[GARDEN_MAX_PLAYER_BULLETS] = { 0 };
    unsigned int i, j;
    combat_legacy_step(input);
    garden_step(g, input);
    compare_prefix(g);
    memset(&old_after, 0, sizeof(old_after));
    combat_legacy_copy(&old_after, offsetof(GardenGame, combat));
    ++compared_steps;
    CHECK(g->combat.count <= GARDEN_MAX_PLAYER_BULLETS);
    CHECK(g->combat.dropped_total == 0);
    if (g->tick == 0) {
        CHECK(g->combat.count == 0);
        CHECK(g->combat.hits_total == 0);
        CHECK(g->combat.kills_total == 0);
        CHECK(g->combat.hit_score_total == 0);
        CHECK(g->combat.kill_score_total == 0);
        CHECK(g->combat.emitted_total == 0);
        CHECK(g->score == 0);
        return;
    }
    CHECK(g->combat.tick == g->tick);
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
        int32_t hp = before.enemies[i].active ? before.enemies[i].hp :
            (g->enemies[i].type == GARDEN_ENEMY_DARTER ? 300 : 3800);
        remaining_hp[i] = hp > 0 ? (uint32_t)hp : 0;
    }
    remaining_hp[GARDEN_MAX_ENEMIES] = before.boss.active ?
        (before.boss.hp > 0 ? (uint32_t)before.boss.hp : 0) :
        (g->boss.max_hp > 0 ? (uint32_t)g->boss.max_hp : 0);
    for (i = 0; i < g->combat.count; ++i) {
        const GardenHitEvent *e = &g->combat.items[i];
        uint32_t nominal = 1u + e->kill_reward;
        uint32_t target = e->target_slot;
        unsigned int matched = GARDEN_MAX_PLAYER_BULLETS;
        CHECK(e->damage > 0);
        CHECK(e->score_delta <= nominal);
        CHECK(e->score_delta == sat_add(replay_score, nominal) - replay_score);
        replay_score = sat_add(replay_score, nominal);
        CHECK(e->killed == !!e->kill_reward);
        if (e->boss) {
            CHECK(e->target_slot == GARDEN_MAX_ENEMIES);
            CHECK(e->target_type == GARDEN_ENEMY_NONE);
            CHECK(e->target_x == g->boss.x);
            CHECK(e->target_y == g->boss.y);
            CHECK(e->target_y >= 0);
            CHECK(e->kill_reward == (e->killed ? 1000u : 0u));
            CHECK(e->x >= e->target_x - 76000 &&
                  e->x <= e->target_x + 76000);
            CHECK(e->y >= e->target_y - 42000 &&
                  e->y <= e->target_y + 42000);
            ++observed_boss_hits;
        } else {
            CHECK(e->target_slot < GARDEN_MAX_ENEMIES);
            CHECK(e->target_type == GARDEN_ENEMY_DARTER ||
                  e->target_type == GARDEN_ENEMY_STOPPER);
            CHECK(e->target_x == g->enemies[e->target_slot].x);
            CHECK(e->target_y == g->enemies[e->target_slot].y);
            CHECK(e->kill_reward == (e->killed ?
                (e->target_type == GARDEN_ENEMY_DARTER ? 20u : 80u) : 0u));
            if (e->target_type == GARDEN_ENEMY_DARTER) {
                CHECK(e->x >= e->target_x - 27000 &&
                      e->x <= e->target_x + 27000);
                CHECK(e->y >= e->target_y + 4000 - 40000 &&
                      e->y <= e->target_y + 4000 + 40000);
            } else {
                CHECK(e->x >= e->target_x - 47000 &&
                      e->x <= e->target_x + 47000);
                CHECK(e->y >= e->target_y - 30000 - 90000 &&
                      e->y <= e->target_y - 30000 + 90000);
            }
        }
        /* Match a real consumed projectile, independently of event identity.
         * Old active slots advanced once; newly spawned slots are age one.
         * Each consumed slot can support only one reported hit. */
        for (j = 0; j < GARDEN_MAX_PLAYER_BULLETS; ++j) {
            const GardenPlayerBullet *p = &g->player_bullets[j];
            if (used_projectile[j] || p->active ||
                p->x != e->x || p->y != e->y || !p->damage)
                continue;
            if ((before.player_bullets[j].active &&
                 p->age == (uint16_t)(before.player_bullets[j].age + 1)) ||
                (!before.player_bullets[j].active && p->age == 1)) {
                matched = j; break;
            }
        }
        CHECK(matched < GARDEN_MAX_PLAYER_BULLETS);
        CHECK(remaining_hp[target] > 0);
        CHECK(e->damage == (remaining_hp[target] <
            g->player_bullets[matched].damage ? remaining_hp[target] :
            g->player_bullets[matched].damage));
        used_projectile[matched] = 1;
        remaining_hp[target] -= e->damage;
        damage_sum[target] += e->damage;
        CHECK(e->killed == (remaining_hp[target] == 0));
        ++hits;
        kills += e->killed;
        nominal_kills += e->kill_reward;
        credited += e->score_delta;
    }
    /* Event damage must account for every real HP change, even when a target
     * leaves the field or dies. The wave-to-Boss cut clears old enemies and
     * is deliberately not treated as damage. */
    if (!(before.mode == GARDEN_MODE_WAVES &&
          g->mode == GARDEN_MODE_BOSS)) {
        for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
            if (before.enemies[i].active || g->enemies[i].active || damage_sum[i]) {
                int32_t old_hp = before.enemies[i].active ? before.enemies[i].hp :
                    (g->enemies[i].type == GARDEN_ENEMY_DARTER ? 300 : 3800);
                int32_t new_hp = g->enemies[i].hp;
                if (old_hp < 0) old_hp = 0;
                if (new_hp < 0) new_hp = 0;
                CHECK(new_hp <= old_hp);
                CHECK(damage_sum[i] == (uint32_t)(old_hp - new_hp));
            }
        }
    } else {
        for (i = 0; i < GARDEN_MAX_ENEMIES; ++i)
            CHECK(damage_sum[i] == 0);
    }
    if (before.boss.active || g->boss.active ||
        damage_sum[GARDEN_MAX_ENEMIES]) {
        int32_t old_hp = before.boss.active ? before.boss.hp : g->boss.max_hp;
        int32_t new_hp = g->boss.hp;
        if (old_hp < 0) old_hp = 0;
        if (new_hp < 0) new_hp = 0;
        CHECK(new_hp <= old_hp);
        CHECK(damage_sum[GARDEN_MAX_ENEMIES] == (uint32_t)(old_hp - new_hp));
    }
    observed_hits += hits;
    observed_kills += kills;
    CHECK(g->combat.hits_total == sat_add(before.combat.hits_total, hits));
    CHECK(g->combat.kills_total == sat_add(before.combat.kills_total, kills));
    CHECK(g->combat.emitted_total == sat_add(before.combat.emitted_total, hits));
    CHECK(g->combat.hit_score_total ==
          sat_add(before.combat.hit_score_total, hits));
    CHECK(g->combat.kill_score_total ==
          sat_add(before.combat.kill_score_total, nominal_kills));
    CHECK(g->score == sat_add(before.score, credited));
    /* All real gameplay epochs remain far from saturation. The independently
     * compiled original awards the retained 20/80/1000 kill score; the exact
     * difference is the new per-projectile hit award. */
    if (g->score < UINT32_MAX && before.score < UINT32_MAX &&
        !(input & GARDEN_INPUT_RESTART))
        CHECK(g->score == old_after.score + g->combat.hit_score_total);
    if (before.tick == g->tick || g->paused ||
        before.mode == GARDEN_MODE_VICTORY ||
        before.mode == GARDEN_MODE_GAMEOVER)
        CHECK(g->combat.count == 0);
}

static void fixture_init(GardenGame *g)
{
    garden_init(g, UINT32_C(0x12345678));
    g->tick = 11;
    g->demo_mode = 0;
    g->player.active = 0;
}

static void enemy_target(GardenGame *g, unsigned int slot, uint8_t type,
                         int32_t hp)
{
    GardenEnemy *e = &g->enemies[slot];
    memset(e, 0, sizeof(*e));
    e->active = 1; e->type = type; e->hp = hp;
    e->x = 320000; e->y = 180000;
    e->phase = 2; e->target_time = 100; e->recycle = 100;
}

static void boss_target(GardenGame *g, int32_t hp)
{
    GardenBoss *b = &g->boss;
    memset(b, 0, sizeof(*b));
    g->mode = GARDEN_MODE_BOSS;
    b->active = 1; b->hp = hp; b->max_hp = 50000;
    b->x = b->target_x = 320000; b->y = 120000;
    b->move_ticks = 100; b->recycle = 100; b->phase_ticks = 100;
}

static void projectile(GardenGame *g, unsigned int slot,
                       int32_t x, int32_t hit_y, uint16_t damage)
{
    GardenPlayerBullet *b = &g->player_bullets[slot];
    memset(b, 0, sizeof(*b));
    b->active = 1; b->x = x; b->y = hit_y + 14000;
    b->vy = -14000; b->damage = damage;
}

static void check_event(const GardenGame *g, unsigned int index,
                        int32_t x, int32_t y, uint16_t damage,
                        uint32_t reward)
{
    const GardenHitEvent *e = &g->combat.items[index];
    CHECK(e->x == x);
    CHECK(e->y == y);
    CHECK(e->damage == damage);
    CHECK(e->killed == !!reward);
    CHECK(e->kill_reward == reward);
    CHECK(e->score_delta == 1u + reward);
}

static void targeted_cases(void)
{
    GardenGame g;
    unsigned int i;
    fixture_init(&g); enemy_target(&g, 0, GARDEN_ENEMY_DARTER, 300);
    projectile(&g, 0, 320000, 184000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 1); CHECK(g.enemies[0].hp == 205);
    CHECK(g.combat.count == 1);
    check_event(&g, 0, 320000, 184000, 95, 0);
    /* One consumed bullet cannot score or emit again on the next step. */
    pair_step(&g, 0); CHECK(g.score == 1); CHECK(g.combat.count == 0);
    CHECK(g.combat.hits_total == 1);

    fixture_init(&g); enemy_target(&g, 2, GARDEN_ENEMY_DARTER, 40);
    projectile(&g, 0, 320000, 184000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 21); CHECK(!g.enemies[2].active);
    CHECK(g.combat.count == 1); CHECK(g.combat.items[0].target_slot == 2);
    check_event(&g, 0, 320000, 184000, 40, 20);
    CHECK(g.effects[0].active);

    fixture_init(&g); enemy_target(&g, 0, GARDEN_ENEMY_STOPPER, 95);
    projectile(&g, 0, 320000, 150000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 81); CHECK(!g.enemies[0].active);
    check_event(&g, 0, 320000, 150000, 95, 80);

    fixture_init(&g); boss_target(&g, 95);
    projectile(&g, 0, 320000, 120000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 1001); CHECK(g.boss.hp == 0);
    CHECK(g.mode == GARDEN_MODE_VICTORY);
    CHECK(g.combat.count == 1); CHECK(g.effects[0].active);
    check_event(&g, 0, 320000, 120000, 95, 1000);
    pair_step(&g, 0); CHECK(g.score == 1001); CHECK(g.combat.count == 0);

    fixture_init(&g); boss_target(&g, 500);
    projectile(&g, 0, 310000, 120000, 95);
    projectile(&g, 1, 330000, 120000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 2); CHECK(g.boss.hp == 310);
    CHECK(g.combat.count == 2); CHECK(g.combat.hits_total == 2);
    check_event(&g, 0, 310000, 120000, 95, 0);
    check_event(&g, 1, 330000, 120000, 95, 0);

    /* Overkill is one real hit with only remaining HP recorded. The second
     * projectile sees a dead target, so cannot add a duplicate kill or hit. */
    fixture_init(&g); boss_target(&g, 40);
    projectile(&g, 0, 310000, 120000, 95);
    projectile(&g, 1, 330000, 120000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 1001); CHECK(g.combat.count == 1);
    check_event(&g, 0, 310000, 120000, 40, 1000);
    CHECK(g.player_bullets[1].active);

    /* Every slot can collide in a single 25ms step: fixed event capacity is
     * sized by actual player projectiles, not an arbitrary truncated queue. */
    fixture_init(&g); boss_target(&g, 50000);
    for (i = 0; i < GARDEN_MAX_PLAYER_BULLETS; ++i)
        projectile(&g, i, 308000 + (int32_t)i * 1000, 120000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.combat.count == 24); CHECK(g.score == 24);
    CHECK(g.boss.hp == 50000 - 24 * 95);
    CHECK(g.combat.emitted_total == 24); CHECK(g.combat.dropped_total == 0);
    for (i = 0; i < 24; ++i)
        check_event(&g, i, 308000 + (int32_t)i * 1000, 120000, 95, 0);

    /* Enemy wins collision priority over an overlapping Boss, unchanged. */
    fixture_init(&g); boss_target(&g, 50000);
    enemy_target(&g, 0, GARDEN_ENEMY_DARTER, 300);
    g.enemies[0].y = 116000;
    projectile(&g, 0, 320000, 120000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.combat.count == 1); CHECK(!g.combat.items[0].boss);
    CHECK(g.boss.hp == 50000); CHECK(g.enemies[0].hp == 205);

    /* Two consumed projectiles at the same position are still two events.
     * Damage is bounded independently and the lethal second hit retains the
     * one original kill reward rather than awarding it twice. */
    fixture_init(&g); boss_target(&g, 96);
    projectile(&g, 0, 320000, 120000, 95);
    projectile(&g, 1, 320000, 120000, 8);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.combat.count == 2); CHECK(g.score == 1002);
    check_event(&g, 0, 320000, 120000, 95, 0);
    check_event(&g, 1, 320000, 120000, 1, 1000);

    /* Inclusive collision boundaries remain the exact original ones. */
    fixture_init(&g); enemy_target(&g, 0, GARDEN_ENEMY_DARTER, 300);
    projectile(&g, 0, 347000, 224000, 95);
    projectile(&g, 1, 347001, 224000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.combat.count == 1); CHECK(g.player_bullets[1].active);
    check_event(&g, 0, 347000, 224000, 95, 0);

    fixture_init(&g); boss_target(&g, 50000); g.boss.y = -1000;
    projectile(&g, 0, 320000, -1000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 0); CHECK(g.combat.count == 0);
    CHECK(g.boss.hp == 50000); CHECK(g.player_bullets[0].active);

    fixture_init(&g); enemy_target(&g, 0, GARDEN_ENEMY_DARTER, 300);
    g.enemies[0].active = 0;
    projectile(&g, 0, 320000, 184000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 0); CHECK(g.combat.count == 0);
    CHECK(g.player_bullets[0].active); CHECK(g.enemies[0].hp == 300);

    fixture_init(&g);
    projectile(&g, 0, 320000, -31000, 95);
    projectile(&g, 1, 100000, 200000, 95);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 0); CHECK(g.combat.count == 0);
    CHECK(!g.player_bullets[0].active); CHECK(g.player_bullets[1].active);

    /* A zero-damage projectile still follows old consumption/flash rules,
     * but is not an effective damage hit and earns no point. */
    fixture_init(&g); boss_target(&g, 50000);
    projectile(&g, 0, 320000, 120000, 0);
    pair_load(&g); pair_step(&g, 0);
    CHECK(g.score == 0); CHECK(g.combat.count == 0);
    CHECK(g.boss.hp == 50000); CHECK(!g.player_bullets[0].active);

    fixture_init(&g); boss_target(&g, 50000);
    projectile(&g, 0, 320000, 120000, 95);
    pair_load(&g); pair_step(&g, 0); CHECK(g.combat.count == 1);
    pair_step(&g, GARDEN_INPUT_PAUSE);
    CHECK(g.paused); CHECK(g.combat.count == 0); CHECK(g.score == 1);
    pair_step(&g, 0); CHECK(g.combat.count == 0); CHECK(g.score == 1);
    pair_step(&g, GARDEN_INPUT_PAUSE);
    CHECK(!g.paused); CHECK(g.combat.count == 0);
    pair_step(&g, GARDEN_INPUT_RESTART);
    CHECK(g.score == 0); CHECK(g.combat.hits_total == 0);
}

static void saturation_cases(void)
{
    GardenGame g;
    uint32_t initial[] = { UINT32_MAX, UINT32_MAX - 1u,
        UINT32_MAX - 20u, UINT32_MAX - 1000u };
    unsigned int i;
    for (i = 0; i < sizeof(initial) / sizeof(initial[0]); ++i) {
        fixture_init(&g); boss_target(&g, 40);
        projectile(&g, 0, 320000, 120000, 95);
        g.score = initial[i];
        garden_step(&g, 0);
        CHECK(g.score == UINT32_MAX);
        CHECK(g.combat.count == 1); CHECK(g.combat.items[0].damage == 40);
        CHECK(g.combat.items[0].kill_reward == 1000);
        CHECK(g.combat.items[0].score_delta == UINT32_MAX - initial[i]);
        CHECK(g.combat.hits_total == 1); CHECK(g.combat.kills_total == 1);
        CHECK(g.combat.hit_score_total == 1);
        CHECK(g.combat.kill_score_total == 1000);
    }
    fixture_init(&g); boss_target(&g, 50000);
    projectile(&g, 0, 320000, 120000, 95);
    g.score = UINT32_MAX;
    g.combat.hits_total = UINT32_MAX;
    g.combat.hit_score_total = UINT32_MAX;
    g.combat.emitted_total = UINT32_MAX;
    garden_step(&g, 0);
    CHECK(g.score == UINT32_MAX); CHECK(g.boss.hp == 49905);
    CHECK(g.combat.items[0].score_delta == 0);
    CHECK(g.combat.hits_total == UINT32_MAX);
    CHECK(g.combat.hit_score_total == UINT32_MAX);
    CHECK(g.combat.emitted_total == UINT32_MAX);
}

static void full_gameplay(void)
{
    GardenGame g;
    unsigned int i, old_mode;
    uint32_t old_tick;
    garden_init(&g, UINT32_C(0x47617264));
    combat_legacy_init(UINT32_C(0x47617264));
    compare_prefix(&g);
    for (i = 0; i < 16000; ++i) {
        uint16_t input = 0;
        if (i == 710 || i == 718 || i == 8190 || i == 8193)
            input = GARDEN_INPUT_PAUSE;
        old_mode = g.mode; old_tick = g.tick;
        pair_step(&g, input);
        if (old_mode != GARDEN_MODE_VICTORY &&
            g.mode == GARDEN_MODE_VICTORY) ++victories;
        if (old_tick > 0 && g.tick == 0) ++automatic_restarts;
    }
    CHECK(victories >= 2); CHECK(automatic_restarts >= 2);
    CHECK(observed_boss_hits > 100);
    /* Same inputs in both cores include manual diagonal/slow movement, mode
     * toggles, pause edges and explicit restarts. Every original state byte
     * except the intentional score change must remain identical. */
    garden_init(&g, UINT32_C(0x5a17c0de));
    combat_legacy_init(UINT32_C(0x5a17c0de));
    for (i = 0; i < 8000; ++i) {
        uint16_t input = 0;
        unsigned int phase = i % 241u;
        if (phase < 50) input = GARDEN_INPUT_LEFT | GARDEN_INPUT_UP;
        else if (phase < 110) input = GARDEN_INPUT_RIGHT | GARDEN_INPUT_SLOW;
        else if (phase < 160) input = GARDEN_INPUT_DOWN;
        else if (phase < 220) input = GARDEN_INPUT_LEFT | GARDEN_INPUT_DOWN |
                                       GARDEN_INPUT_SLOW;
        if (i % 907u == 906u) input = GARDEN_INPUT_RESTART;
        if (i == 83 || i == 91 || i == 2703 || i == 2711)
            input = GARDEN_INPUT_PAUSE;
        if (i % 421u == 300u) input = GARDEN_INPUT_TOGGLE_DEMO;
        pair_step(&g, input);
    }
    CHECK(observed_hits > 100);
    CHECK(observed_kills > 10);
}

int main(void)
{
    CHECK(combat_legacy_size() == offsetof(GardenGame, combat));
    targeted_cases();
    saturation_cases();
    full_gameplay();
    printf("combat core checks=%lu prefix_compared_steps=%lu observed_hits=%lu "
           "observed_kills=%lu boss_hits=%lu victories=%lu auto_restarts=%lu\n",
           checks, compared_steps, observed_hits, observed_kills,
           observed_boss_hits, victories, automatic_restarts);
    puts("PASS exact legacy physics prefix; effective projectile +1; "
         "20/80/1000 kill rewards; authoritative event geometry; pause/restart; "
         "24-event capacity; bounded overkill; saturating score.");
    return 0;
}
