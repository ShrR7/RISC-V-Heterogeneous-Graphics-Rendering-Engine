/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reduced Garden of Coloured Lights 1.0.9 game core for Ti60.
 * Original game: Linley Henzell; upstream distribution: garden.sourceforge.net.
 * See ../vendor/garden-1.0.9/COPYING for the GNU GPL version 3 or later.
 *
 * This is a MODIFIED, PARTIAL PORT, not the complete original game:
 * - Stage 1's DARTER1 / STOPPER1 content is shortened to two groups and Boss 1.
 * - Boss 1's three original attack generators are retained, on easy difficulty.
 *   Selection is a deterministic showcase cycle instead of HP/random gating.
 *   The C rhythm edition adds a genuine charge/pulse-train/recovery schedule;
 *   HP bands70%/40% shorten that schedule, not projectile geometry or damage.
 * - Original 40 Hz, GRAIN=1000, ANGLE=1024 and projectile movement are retained.
 * - Original float trigonometric tables become a Q15 quarter-wave integer table.
 *   Original rand() becomes xorshift32, so patterns are seeded/reproducible but
 *   are not bit-identical to upstream rand()/float trajectories.
 * - One fixed level-5 machine gun is automatically fired; upgrades/orbitals,
 *   three-weapon selection, music synchronization, side turrets and sound
 *   are omitted.
 * - Explicit geometric collisions replace upstream render-dependent detection.
 * - Object pools are reduced. If full, new shots are discarded and counted.
 * - Automatic pilot, compact score, pause/restart, effects and end-screen loop
 *   are demonstration additions. The pilot is vulnerable and uses normal lives.
 * - This reward edition adds one graze per activated bullet (+5), real falling
 *   and magnetic pickups (+25), two non-damaging clear/invulnerability bombs,
 *   an eight-second end state and a runtime-only retained high score. The
 *   automatic pilot uses the same finite bombs and real pickup movement.
 */
#include "garden_core.h"
#include <string.h>

typedef char garden_state_must_fit_20k[
    sizeof(GardenGame) < 20u * 1024u ? 1 : -1];

#ifdef GARDEN_HOST_ALLOC_PROBES
/* Host-only workload accounting. Absent from the board binary. */
extern void garden_host_alloc_probe(unsigned boss,unsigned slot);
#endif

/* Generated offline: round(sin(i * pi / 512) * 32767), i=0..256.
 * Replaces src/stuff.c:49-88 float tables/xpart/ypart. No runtime libm.
 */
static const int16_t quarter_sine[257] = {
    0, 201, 402, 603, 804, 1005, 1206, 1407, 1608, 1809, 2009, 2210,
    2410, 2611, 2811, 3012, 3212, 3412, 3612, 3811, 4011, 4210, 4410, 4609,
    4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195, 6393, 6590, 6786, 6983,
    7179, 7375, 7571, 7767, 7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319,
    9512, 9704, 9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605,
    11793, 11980, 12167, 12353, 12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269, 15446, 15623, 15800, 15976,
    16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000,
    20159, 20317, 20475, 20631, 20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027, 23170, 23311, 23452, 23592,
    23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674,
    26790, 26905, 27019, 27133, 27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803, 28898, 28992, 29085, 29177,
    29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050,
    31113, 31176, 31237, 31297, 31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098, 32137, 32176, 32213, 32250,
    32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752,
    32757, 32761, 32765, 32766, 32767,
};

static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t combat_add(uint32_t value,uint32_t increment)
{ return UINT32_MAX-value<increment ? UINT32_MAX : value+increment; }

static uint32_t reward_score(GardenGame *g,uint32_t amount)
{
    uint32_t before=g->score;
    g->score=combat_add(g->score,amount);
    if (g->score>g->reward.high_score) g->reward.high_score=g->score;
    return g->score-before;
}

static void drop_pickup(GardenGame *g,int32_t x,int32_t y)
{
    for (unsigned i=0U;i<GARDEN_MAX_PICKUPS;i++) {
        GardenPickup *p=&g->pickups[i];
        if (p->active) continue;
        *p=(GardenPickup){.x=clamp32(x,5000,635000),
            .y=clamp32(y,5000,460000),.vx=(i&1U) ? 600 : -600,
            .vy=-1800,.active=1U};
        g->reward.items_spawned=combat_add(g->reward.items_spawned,1U);
        return;
    }
    g->reward.items_dropped=combat_add(g->reward.items_dropped,1U);
}

static void run_pickups(GardenGame *g)
{
    unsigned count=0U;
    for (unsigned i=0U;i<GARDEN_MAX_PICKUPS;i++) {
        GardenPickup *p=&g->pickups[i];
        if (!p->active) continue;
        ++p->age;
        int32_t dx=g->player.x-p->x,dy=g->player.y-p->y;
        unsigned can_collect=g->player.active && !g->player.respawning;
        /* Both pilots use the same96px attraction during live play. Victory
         * collects survivors across the cleared field for the end sequence;
         * credit is solely from actual contact, never a timed counter.
         */
        if (can_collect && p->age>=20U &&
            ((g->mode==GARDEN_MODE_VICTORY && g->demo_mode) ||
             (abs32(dx)<=96000 && abs32(dy)<=96000)))
            p->magnet=1U;
        if (p->magnet && can_collect) {
            p->vx=clamp32(dx/6,-12000,12000);
            p->vy=clamp32(dy/6,-12000,12000);
        } else {
            p->vx=p->vx*15/16;
            p->vy=clamp32(p->vy+180,-1800,1800);
        }
        p->x+=p->vx; p->y+=p->vy;
        if (can_collect && abs32(g->player.x-p->x)<=14000 &&
            abs32(g->player.y-p->y)<=14000) {
            p->active=0U;
            g->reward.items_collected=combat_add(g->reward.items_collected,1U);
            g->reward.item_score_total=combat_add(g->reward.item_score_total,
                                                   reward_score(g,GARDEN_PICKUP_SCORE));
            g->reward.pickup_flash_x=g->player.x;
            g->reward.pickup_flash_y=g->player.y;
            g->reward.pickup_flash_ticks=16U;
        } else if (p->age>=480U || p->y>510000) p->active=0U;
        count+=p->active!=0U;
    }
    g->reward.pickup_count=(uint8_t)count;
}

static void fire_bomb(GardenGame *g)
{
    if (!g->reward.bombs || !g->player.active || g->player.respawning ||
        (g->mode!=GARDEN_MODE_WAVES && g->mode!=GARDEN_MODE_BOSS)) return;
    unsigned cleared=0U;
    for (unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++) {
        cleared+=g->enemy_bullets[i].active!=0U;
        g->enemy_bullets[i].active=0U;
    }
    --g->reward.bombs;
    g->reward.bombs_used=combat_add(g->reward.bombs_used,1U);
    g->reward.bullets_cleared=combat_add(g->reward.bullets_cleared,cleared);
    g->reward.bomb_flash_ticks=12U;
    g->enemy_bullet_count=0U;
    if (g->player.grace<40U) g->player.grace=40U;
}

static int32_t sine(int angle)
{
    unsigned int a = (unsigned int)angle & 1023u;
    unsigned int q = a >> 8, p = a & 255u;
    int32_t v = quarter_sine[(q & 1u) ? 256u - p : p];
    return q >= 2u ? -v : v;
}

static int32_t ypart(int angle, int length)
{
    /* Selected generators use lengths <= 24000, so products fit int32_t.
     * Splitting also accommodates larger lengths without a 64-bit divide.
     */
    int32_t s = sine(angle);
    return (length / 32767) * s + ((length % 32767) * s) / 32767;
}
static int32_t xpart(int angle, int length)
{ return ypart(angle + 256, length); }

static uint32_t random_u32(GardenGame *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}
static int grand(GardenGame *g, unsigned int n)
{ return n ? (int)(random_u32(g) % n) : 0; }

/* atan2 replacement, angle zero east / positive y south. Scale displacement
 * down before cross products; binary search gives one-angle-unit resolution.
 */
static uint16_t aim_at(int32_t dx, int32_t dy)
{
    uint32_t ax = (uint32_t)abs32(dx), ay = (uint32_t)abs32(dy);
    unsigned int lo = 0, hi = 256, m, a;
    if (!ax && !ay) return 256;
    while (ax > 32767u || ay > 32767u) { ax >>= 1; ay >>= 1; }
    while (hi - lo > 1u) {
        m = (lo + hi) / 2u;
        if (ax * (uint32_t)(uint16_t)quarter_sine[m] <
            ay * (uint32_t)(uint16_t)quarter_sine[256u - m]) lo = m;
        else hi = m;
    }
    a = hi;
    if (!ax) a = 256;
    if (!ay) a = 0;
    if (dx < 0) a = 512u - a;
    if (dy < 0) a = (1024u - a) & 1023u;
    return (uint16_t)a;
}

static uint16_t turn_towards(uint16_t a, uint16_t target, int amount)
{
    int d = ((int)target - (int)a + 1536) & 1023;
    d -= 512;
    if (d > amount) d = amount;
    if (d < -amount) d = -amount;
    return (uint16_t)((a + d) & 1023);
}

static void add_effect(GardenGame *g, int32_t x, int32_t y, uint8_t big)
{
    unsigned int i;
    for (i = 0; i < GARDEN_MAX_EFFECTS; ++i) {
        GardenEffect *f = &g->effects[i];
        if (!f->active) {
            f->active = 1; f->age = 0; f->big = big; f->x = x; f->y = y;
            return;
        }
    }
}

static GardenEnemyBullet *new_bullet(GardenGame *g, uint8_t type,
                                    int32_t x, int32_t y,
                                    int angle, int colour, int wait)
{
    unsigned int i;
    /* A Boss pulse never frees slots or yields between these calls. Every
     * earlier examined slot is still occupied, so the next minimum free slot
     * is at or after the preceding successful allocation. Wave callers retain
     * their original independent scan from0, including full-pool drop behavior.
     */
    for (i = g->boss_alloc_enabled ? g->boss_alloc_cursor : 0U;
         i < GARDEN_MAX_ENEMY_BULLETS; ++i) {
#ifdef GARDEN_HOST_ALLOC_PROBES
        garden_host_alloc_probe(g->boss_alloc_enabled!=0U,i);
#endif
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        if (!b->active) {
            if (g->boss_alloc_enabled) g->boss_alloc_cursor=(uint16_t)(i+1U);
            memset(b, 0, sizeof(*b));
            b->active = 1; b->type = type;
            b->x = b->cord_x = x; b->y = b->cord_y = y;
            b->angle = b->cord_angle = (uint16_t)(angle & 1023);
            b->colour = (uint8_t)colour; b->cord_wait = (int16_t)wait;
            b->ttl = 500;
            return b;
        }
    }
    ++g->dropped_enemy_bullets;
    return 0;
}

/* Directly adapted from enemy.c:4633-4657 fire_dart. Darts start at rest
 * and accelerate for 59 steps (time increment occurs before the <60 test).
 */
static void fire_dart(GardenGame *g, int32_t x, int32_t y, int angle,
                      int colour, int wait, int accel)
{
    GardenEnemyBullet *b = new_bullet(g, GARDEN_BULLET_DART,
                                     x, y, angle, colour, wait);
    if (b) b->accel = (int16_t)accel;
}

/* Directly adapted from enemy.c:4715-4740 fire_diamond. Rotation changes
 * appearance, not its straight velocity; this is not EBULLET_CURVE.
 */
static void fire_diamond(GardenGame *g, int32_t x, int32_t y, int angle,
                         int colour, int speed, int rot, int wait)
{
    GardenEnemyBullet *b = new_bullet(g, GARDEN_BULLET_DIAMOND,
                                     x, y, angle, colour, wait);
    if (b) {
        b->vx = xpart(angle, speed); b->vy = ypart(angle, speed);
        b->spin = (int16_t)rot;
    }
}

/* Boss 1 attack 1: enemy.c:459-539. Retains all four status variants,
 * 32 pairs, 32/16-unit angle offsets, 19000 origin radius and acceleration.
 */
static void boss_attack1(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    int i, angle = 0, a1 = 0, xa = 8, accel1, accel2;
    if (b->status == 0) angle = grand(g, 1024);
    else a1 = grand(g, 1024);
    if (b->status == 3 && grand(g, 2) == 0) xa = 4;
    for (i = 0; i < 32; ++i) {
        angle += 32;
        if (b->status == 0) {
            accel1 = 50 + abs32(xpart(angle * 2, 30));
            accel2 = 30 + abs32(xpart((angle + 16) * 2, 10));
        } else if (b->status == 1) {
            accel1 = 50 + ypart(angle * 2 + a1, 30);
            accel2 = 30 + abs32(ypart((angle + 16) * 2 + a1, 10));
        } else if (b->status == 2) {
            accel1 = 50 + ypart(angle * 2 + a1, 15) +
                     abs32(xpart(angle * 4, 15));
            accel2 = 30 + abs32(ypart((angle + 16) * 2 + a1 * 4, 10)) +
                     abs32(xpart(angle * 4, 10));
        } else {
            accel1 = 50 + ypart(angle * 2 + a1, 10) +
                     abs32(xpart(angle * xa, 25));
            accel2 = 30 + abs32(ypart((angle + 16) * 2 + a1 * 4, 5)) +
                     abs32(xpart(angle * xa, 15));
        }
        fire_dart(g, b->x + xpart(angle, 19000),
                  b->y + ypart(angle, 19000), angle, 3, 80, accel1);
        fire_dart(g, b->x + xpart(angle, 19000),
                  b->y + ypart(angle, 19000), angle + 16, 5, 100, accel2);
    }
    b->status = b->status == 0 ? 1 : (uint8_t)grand(g, 4);
    ++g->boss_attack_shots[0];
}

/* Boss 1 attack 2: enemy.c:542-571, easy difficulty (3 iterations).
 * Three arms with ANGLE_3=341, adjacent spokes shifted by ANGLE_32=32.
 */
static void boss_attack2(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    int i, k, angle = grand(g, 1024);
    for (i = 0; i < 3; ++i) {
        for (k = 0; k < 3; ++k)
            fire_dart(g, b->x + xpart(angle + k * 341, 19000),
                      b->y + ypart(angle + k * 341, 19000),
                      angle + k * 341, 5, 80, 60 + i * 20);
        angle += 32;
    }
    ++g->boss_attack_shots[1];
}

/* Boss 1 attack 3: enemy.c:573-691. Upstream line 584 forces stat=3,
 * so only the actually reachable easy branch at 666-685 is ported:
 * 12 groups of three straight/visually rotating diamonds, original speeds.
 */
static void boss_attack3(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    int i, angle = grand(g, 1024);
    for (i = 0; i < 12; ++i) {
        int32_t x = b->x + xpart(angle, 19000);
        int32_t y = b->y + ypart(angle, 19000);
        fire_diamond(g, x, y, angle, 2, 2200 + xpart(i * 21, 600),
                     xpart(i * 21, 8), 80);
        fire_diamond(g, x, y, angle + 1024 / 21, 1,
                     2000 + xpart(i * 21, 600),
                     xpart(i * 21 + 512, 12), 40);
        fire_diamond(g, x, y, angle + 1024 / 21, 1,
                     2400 + xpart(i * 21, 600),
                     xpart(i * 21 + 512, 12), 40);
        angle += 1024 / 12;
    }
    b->status = (uint8_t)grand(g, 3);
    ++g->boss_attack_shots[2];
}

static unsigned boss_pulse_size(unsigned attack)
{ return attack==1U ? 64U : attack==2U ? 9U : 36U; }

static unsigned boss_free_bullets(const GardenGame *g)
{
    unsigned count=0U;
    for (unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)
        count+=g->enemy_bullets[i].active==0U;
    unsigned active=GARDEN_MAX_ENEMY_BULLETS-count;
    unsigned budget_free=active<GARDEN_BOSS_BULLET_BUDGET ?
                         GARDEN_BOSS_BULLET_BUDGET-active : 0U;
    /* This bounds live projectiles, not the array. Count the actual slots
     * rather than previous-step enemy_bullet_count; both complete trains and
     * individual pulses use min(physical free slots, remaining live budget).
     */
    return count<budget_free ? count : budget_free;
}

static unsigned boss_has_train_space(const GardenGame *g)
{
    return boss_free_bullets(g)>=boss_pulse_size(g->boss.attack)*g->boss.burst_left;
}

unsigned garden_boss_fire_countdown(const GardenGame *g)
{
    if (!g || g->mode!=GARDEN_MODE_BOSS || !g->boss.active ||
        g->boss.rhythm_state!=GARDEN_BOSS_CHARGE ||
        !g->boss.burst_left || !g->boss.rhythm_ticks || g->boss.rhythm_ticks>20U ||
        !boss_has_train_space(g)) return 0U;
    return g->boss.rhythm_ticks;
}

static void boss_schedule_charge(GardenGame *g,unsigned fresh_train)
{
    GardenBoss *b=&g->boss;
    unsigned band=b->max_hp>0 && (int64_t)b->hp*100<= (int64_t)b->max_hp*40 ? 2U :
                  b->max_hp>0 && (int64_t)b->hp*100<= (int64_t)b->max_hp*70 ? 1U : 0U;
    if (b->hp_band!=band) g->rhythm.hp_band_changes++;
    b->hp_band=(uint8_t)band;
    b->charge_ticks=(uint16_t)(20U-4U*band);
    b->rest_ticks=(uint16_t)(80U-16U*band);
    b->burst_gap=(uint8_t)(b->attack==2U ? 6U-band : 12U-2U*band);
    /* Every entire train fits the80-live board budget:64,54,72. In
     * particular, retaining two64-projectile rings would wait forever.
     */
    if (fresh_train) b->burst_left=(uint8_t)(b->attack==1U ? 1U :
                                           b->attack==2U ? 6U : 2U);
    if (!boss_has_train_space(g)) {
        b->rhythm_state=GARDEN_BOSS_WAIT; b->rhythm_ticks=0U; b->recycle=0U;
        return;
    }
    b->rhythm_state=GARDEN_BOSS_CHARGE;
    b->rhythm_ticks=b->charge_ticks; b->recycle=b->rhythm_ticks;
    g->rhythm.charge_windows++;
}

static void boss_fire_pulse(GardenGame *g)
{
    GardenBoss *b=&g->boss;
    /* Admit a complete generator call, never a truncated half-ring. Normally
     * the whole train was reserved before charging; this second check also
     * safely handles externally constructed snapshots/competing producers.
     */
    if (boss_free_bullets(g)<boss_pulse_size(b->attack)) {
        b->rhythm_state=GARDEN_BOSS_WAIT; b->rhythm_ticks=0U; b->recycle=0U;
        g->rhythm.shots_deferred++;
        return;
    }
    g->boss_alloc_cursor=0U; g->boss_alloc_enabled=1U;
    if (b->attack==1U) boss_attack1(g);
    else if (b->attack==2U) boss_attack2(g);
    else boss_attack3(g);
    g->boss_alloc_cursor=0U; g->boss_alloc_enabled=0U;
    g->rhythm.shots_admitted++;
    if (b->burst_left) b->burst_left--;
    if (b->burst_left) {
        b->rhythm_state=GARDEN_BOSS_BURST; b->rhythm_ticks=b->burst_gap;
    } else {
        b->rhythm_state=GARDEN_BOSS_REST; b->rhythm_ticks=b->rest_ticks;
        g->rhythm.bursts_completed++; g->rhythm.rest_windows++;
    }
    b->recycle=b->rhythm_state==GARDEN_BOSS_BURST ? b->rhythm_ticks : 0U;
}

static void start_boss(GardenGame *g)
{
    memset(g->enemies, 0, sizeof(g->enemies));
    /* Cut to Boss after the shortened waves; discard leftover wave bullets. */
    memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
    memset(&g->boss, 0, sizeof(g->boss));
    g->boss.active = 1; g->boss.x = 320000; g->boss.y = -80000;
    g->boss.target_x = g->boss.x;
    g->boss.vy = 3000; g->boss.move_ticks = 100;
    /* level.c:1617-1633 original Boss 1 HP and 100-tick entrance. */
    g->boss.hp = g->boss.max_hp = 50000;
    g->boss.attack = 1; g->boss.rhythm_state=GARDEN_BOSS_TRANSIT;
    g->mode = GARDEN_MODE_BOSS;
}

static void run_boss(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    if (!b->active) return;
    if (b->flash) --b->flash;
    if (b->move_ticks) {
        --b->move_ticks;
        if (b->y < 120000) b->y += b->vy;
        /* Demonstration transit: simpler than original fold/drag/target AI. */
        b->x += (b->target_x - b->x) / 20;
        if (!b->move_ticks) {
            b->target_x = b->x;
            g->rhythm.transits_completed++;
            boss_schedule_charge(g,1U);
        }
        return;
    }
    if (b->rhythm_state==GARDEN_BOSS_WAIT) {
        g->rhythm.capacity_wait_ticks++;
        if (boss_has_train_space(g)) boss_schedule_charge(g,0U);
        return;
    }
    if (b->rhythm_state==GARDEN_BOSS_TRANSIT) {
        boss_schedule_charge(g,1U); return;
    }
    if (b->rhythm_ticks) --b->rhythm_ticks;
    b->recycle=(b->rhythm_state==GARDEN_BOSS_CHARGE ||
                b->rhythm_state==GARDEN_BOSS_BURST) ? b->rhythm_ticks : 0U;
    if (b->rhythm_ticks) return;
    if (b->rhythm_state==GARDEN_BOSS_CHARGE || b->rhythm_state==GARDEN_BOSS_BURST) {
        if (b->rhythm_state==GARDEN_BOSS_CHARGE && !boss_has_train_space(g)) {
            b->rhythm_state=GARDEN_BOSS_WAIT;
            g->rhythm.shots_deferred++;
        } else boss_fire_pulse(g);
    } else if (b->rhythm_state==GARDEN_BOSS_REST) {
        b->attack=(uint8_t)(b->attack==3U ? 1U : b->attack+1U);
        b->move_ticks=40U; b->rhythm_state=GARDEN_BOSS_TRANSIT;
        b->target_x=150000+grand(g,390000);
    }
}

static void spawn_enemy(GardenGame *g, uint8_t type, int32_t x, int32_t y)
{
    unsigned int i;
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
        GardenEnemy *e = &g->enemies[i];
        if (!e->active) {
            memset(e, 0, sizeof(*e)); e->active = 1; e->type = type;
            e->x = x; e->y = y; e->aim_angle = 256; e->angle = 256;
            /* eclass.c:51-80: original DARTER1 300HP, STOPPER1 3800HP. */
            e->hp = type == GARDEN_ENEMY_DARTER ? 300 : 3800;
            e->vy = type == GARDEN_ENEMY_DARTER ? 6000 : 2000;
            e->target_time = type == GARDEN_ENEMY_DARTER ? 40 : 90;
            return;
        }
    }
}

/* Stage 1 selection: level.c:292 (STOPPER1 + DARTER1), 295 (STOPPER1),
 * 319 (Boss 1); group of four with delay 80 from level.c:1069-1097.
 * Total lead-in is intentionally shortened; not the original stage schedule.
 */
static void run_waves(GardenGame *g)
{
    uint32_t t = g->tick;
    if ((t >= 40 && t <= 280 && (t - 40) % 80 == 0) ||
        (t >= 400 && t <= 640 && (t - 400) % 80 == 0)) {
        unsigned int n = (unsigned int)((t >= 400 ? t - 400 : t - 40) / 80);
        spawn_enemy(g, GARDEN_ENEMY_DARTER,
                    100000 + (int32_t)n * 120000,
                    -30000 - grand(g, 140000));
    }
    if (t == 40 || t == 680)
        spawn_enemy(g, GARDEN_ENEMY_STOPPER,
                    220000 + grand(g, 200000), -30000);
    if (t >= 1000) start_boss(g);
}

/* Easy STOPPER1 burst, enemy.c:4246-4305: six sets of acceleration
 * 120/100/80, ANGLE_1/30 steps and alternating winding. Music sync omitted.
 */
static void stopper_fire(GardenGame *g, const GardenEnemy *e)
{
    int i, a = grand(g, 1024), inc = 1024 / 30;
    if (grand(g, 2) == 0) inc = -inc;
    for (i = 0; i < 6; ++i) {
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, 5, 50, 120); a += inc;
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, 5, 50, 100); a += inc;
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, 5, 50, 80); a += inc * 2;
    }
}

static void run_enemies(GardenGame *g)
{
    unsigned int i;
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
        GardenEnemy *e = &g->enemies[i];
        if (!e->active) continue;
        ++e->age; if (e->flash) --e->flash;
        if (e->recycle) --e->recycle;
        --e->target_time;
        /* Darter phase timings from enemy.c:2812-2949. Stopper phase
         * timings from enemy.c:4023-4064 and eclass.c:66-81.
         */
        if (e->target_time <= 0) {
            if (e->phase == 0) {
                e->phase = 1;
                e->target_time = e->type == GARDEN_ENEMY_DARTER ? 30 : 60;
            } else if (e->phase == 1) {
                e->phase = 2; e->vx = e->vy = 0;
                e->target_time = e->type == GARDEN_ENEMY_DARTER ? 90 : 150;
            } else if (e->phase == 2) {
                e->phase = 3;
                e->target_time = e->type == GARDEN_ENEMY_DARTER ? 30 : 60;
            } else if (e->phase == 3) {
                if (e->type == GARDEN_ENEMY_STOPPER || e->moves >= 3) {
                    e->phase = 5; e->target_time = 1000; e->vx = 0;
                } else {
                    e->phase = 4; ++e->moves; e->target_time = 1000;
                    /* config.h:83-84 RAND_X_IN / RAND_Y_IN. */
                    e->target_x = 80000 + grand(g, 475000);
                    e->target_y = 80000 + grand(g, 315000);
                    e->angle = aim_at(e->target_x - e->x, e->target_y - e->y);
                    e->vx = e->vy = 0;
                }
            }
        }
        e->aim_angle = turn_towards(e->aim_angle,
                                   aim_at(g->player.x - e->x,
                                          g->player.y - e->y), 8);
        if (e->phase == 1 || e->phase == 3) {
            e->vx = e->vx * 910 / 1000; e->vy = e->vy * 910 / 1000;
        } else if (e->phase == 2 && !e->recycle) {
            if (e->type == GARDEN_ENEMY_DARTER) {
                /* enemy.c:2864-2877, difficulty zero. */
                fire_dart(g, e->x + xpart(e->aim_angle, 7000),
                          e->y + ypart(e->aim_angle, 7000),
                          e->aim_angle, 3, 50, 80);
                e->recycle = 40;
            } else { stopper_fire(g, e); e->recycle = 90; }
        } else if (e->phase == 4) {
            if (e->target_time > 980) {
                e->vx += xpart(e->angle, 250); e->vy += ypart(e->angle, 250);
            }
            if (abs32(e->x - e->target_x) < 10000 &&
                abs32(e->y - e->target_y) < 10000) {
                e->phase = 1; e->target_time = 30;
            }
        } else if (e->phase == 5 && e->target_time > 980) {
            /* enemy.c:2939-2942 Darter; 4502-4504 Stopper. */
            e->vy += e->type == GARDEN_ENEMY_DARTER ? 250 : 2000 / 17;
        }
        e->x += e->vx; e->y += e->vy;
        if ((e->phase == 5 && (e->y > 550000 || e->x < -100000 ||
                               e->x > 740000)) || e->age > 1600)
            e->active = 0;
    }
}

/* Original bullet movement and cord activation from ebullet.c:291-308,
 * 341-379. Decorations omitted, activation semantics retained.
 */
static void run_enemy_bullets(GardenGame *g)
{
    unsigned int i;
    for (i = 0; i < GARDEN_MAX_ENEMY_BULLETS; ++i) {
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        if (!b->active) continue;
        ++b->age;
        if (b->type == GARDEN_BULLET_DART && b->age < 60) {
            b->vx += xpart(b->angle, b->accel);
            b->vy += ypart(b->angle, b->accel);
        }
        b->x += b->vx; b->y += b->vy;
        if (b->type == GARDEN_BULLET_DIAMOND)
            b->angle = (uint16_t)((b->angle + b->spin) & 1023);
        if (b->cord_wait > 0) --b->cord_wait;
        else if (b->cord_wait == 0) {
            b->cord_vx += xpart(b->cord_angle, 200);
            b->cord_vy += ypart(b->cord_angle, 200);
            b->cord_x += b->cord_vx; b->cord_y += b->cord_vy;
            if (abs32(b->cord_x - b->x) <= 9000 &&
                abs32(b->cord_y - b->y) <= 9000) b->cord_wait = -1;
        }
        if (b->ttl) --b->ttl;
        if (!b->ttl || (b->cord_wait < 0 &&
            (b->x <= -10000 || b->x >= 650000 ||
             b->y <= -10000 || b->y >= 490000))) b->active = 0;
    }
}

static void fire_player(GardenGame *g)
{
    unsigned int i, side;
    /* input.c:632-663 WPN_MGUN, fixed level 5, one weapon slot. Original
     * recycle 1000 / (70 + 5*7) takes ten ticks. Auto fire is a port change.
     */
    if (g->player.recycle) { --g->player.recycle; return; }
    for (side = 0; side < 2; ++side) {
        for (i = 0; i < GARDEN_MAX_PLAYER_BULLETS; ++i) {
            GardenPlayerBullet *b = &g->player_bullets[i];
            if (!b->active) {
                memset(b, 0, sizeof(*b)); b->active = 1;
                b->x = g->player.x + (side ? 4000 : -4000);
                b->y = g->player.y - 18000; b->vy = -14000; b->damage = 95;
                break;
            }
        }
        if (i == GARDEN_MAX_PLAYER_BULLETS) ++g->dropped_player_bullets;
    }
    g->player.recycle = 9;
}

static void damage_player(GardenGame *g)
{
    if (!g->player.active || g->player.grace || g->player.respawning) return;
    g->reward.hits_taken=combat_add(g->reward.hits_taken,1U);
    add_effect(g, g->player.x, g->player.y, 1);
    if (g->lives) --g->lives;
    if (!g->lives) {
        g->player.active = 0; g->mode = GARDEN_MODE_GAMEOVER; g->end_ticks = 0;
        return;
    }
    /* game.c:1002-1006 original 50-tick respawn / 180-tick invulnerability,
     * x=320000/y=520000; input.c:148-152 moves up 4000 each respawn tick. */
    g->player.x = 320000; g->player.y = 520000; g->player.recycle = 0;
    g->player.respawning = 50; g->player.grace = 180;
}

static int overlaps(int32_t x, int32_t y, int32_t cx, int32_t cy,
                    int32_t rx, int32_t ry)
{ return abs32(x - cx) <= rx && abs32(y - cy) <= ry; }

static void combat_reward(GardenGame *g,const GardenPlayerBullet *bullet,
                          int32_t before_hp,int32_t target_x,int32_t target_y,
                          unsigned slot,unsigned type,unsigned boss,
                          unsigned killed,unsigned reward)
{
    unsigned effective=before_hp>0 ? bullet->damage : 0U;
    if (effective>(uint32_t)(before_hp>0 ? before_hp : 0))
        effective=(unsigned)before_hp;
    uint32_t before=g->score;
    reward_score(g,reward+(effective ? 1U : 0U));
    if (!effective) return;
    GardenCombatEvents *out=&g->combat;
    out->hits_total=combat_add(out->hits_total,1U);
    out->hit_score_total=combat_add(out->hit_score_total,1U);
    if (killed) {
        out->kills_total=combat_add(out->kills_total,1U);
        out->kill_score_total=combat_add(out->kill_score_total,reward);
    }
    out->emitted_total=combat_add(out->emitted_total,1U);
    if (out->count>=GARDEN_COMBAT_EVENT_MAX) {
        out->dropped_total=combat_add(out->dropped_total,1U);
        return;
    }
    out->items[out->count++]=(GardenHitEvent){
        .x=bullet->x,.y=bullet->y,.target_x=target_x,.target_y=target_y,
        .score_delta=g->score-before,.kill_reward=reward,
        .damage=(uint16_t)effective,.target_slot=(uint8_t)slot,
        .target_type=(uint8_t)type,.killed=(uint8_t)killed,.boss=(uint8_t)boss
    };
}

static void run_player_bullets(GardenGame *g)
{
    unsigned int i, j;
    for (i = 0; i < GARDEN_MAX_PLAYER_BULLETS; ++i) {
        GardenPlayerBullet *b = &g->player_bullets[i];
        if (!b->active) continue;
        ++b->age; b->x += b->vx; b->y += b->vy;
        for (j = 0; j < GARDEN_MAX_ENEMIES; ++j) {
            GardenEnemy *e = &g->enemies[j];
            int32_t rx = e->type == GARDEN_ENEMY_DARTER ? 27000 : 47000;
            int32_t ry = e->type == GARDEN_ENEMY_DARTER ? 40000 : 90000;
            int32_t offy = e->type == GARDEN_ENEMY_DARTER ? 4000 : -30000;
            if (e->active && overlaps(b->x, b->y, e->x, e->y + offy, rx, ry)) {
                /* eclass.c sizes/offsets + MGUN sizes, pbullet.c:505-548. */
                int32_t before_hp=e->hp;
                unsigned killed,reward=0U;
                e->hp -= b->damage; e->flash = 3; b->active = 0;
                killed=e->hp<=0;
                if (killed) {
                    add_effect(g, e->x, e->y, e->type == GARDEN_ENEMY_STOPPER);
                    drop_pickup(g,e->x,e->y);
                    reward=e->type == GARDEN_ENEMY_DARTER ? 20u : 80u;
                    e->active = 0;
                }
                combat_reward(g,b,before_hp,e->x,e->y,j,e->type,0U,killed,reward);
                break;
            }
        }
        if (b->active && g->boss.active && g->boss.y >= 0 &&
            overlaps(b->x, b->y, g->boss.x, g->boss.y, 76000, 42000)) {
            /* Compact static boss silhouette replaces original animated parts. */
            int32_t before_hp=g->boss.hp;
            unsigned killed;
            g->boss.hp -= b->damage; g->boss.flash = 3; b->active = 0;
            killed=g->boss.hp<=0;
            if (killed) {
                g->boss.hp = 0; g->boss.active = 0;
                add_effect(g, g->boss.x, g->boss.y, 1);
                drop_pickup(g,g->boss.x,g->boss.y);
                memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
                g->mode = GARDEN_MODE_VICTORY; g->end_ticks = 0;
            }
            combat_reward(g,b,before_hp,g->boss.x,g->boss.y,GARDEN_MAX_ENEMIES,
                          GARDEN_ENEMY_NONE,1U,killed,killed ? 1000U : 0U);
            if (killed) {
                /* The event must be emitted before clearing its projectile.
                 * Frozen end-state rendering must not retain white shot bars
                 * or unrelated enemies around the staged Boss destruction.
                 */
                memset(g->player_bullets,0,sizeof(g->player_bullets));
                memset(g->enemies,0,sizeof(g->enemies));
                g->player_bullet_count=g->enemy_bullet_count=g->enemy_count=0U;
                break;
            }
        }
        if (b->y < -30000 || b->y > 510000 || b->age > 90) b->active = 0;
    }
}

/* Demonstration addition. Tracks a target, slowly sweeps, and repels nearby
 * active bullets. It has no immunity, teleport or scripted score/HP changes.
 */
static unsigned auto_pilot(GardenGame *g, int32_t *vx, int32_t *vy)
{
    int32_t tx = 320000, ty = 405000, repel_x = 0, repel_y = 0;
    unsigned near_danger=0U,bomb_danger=0U;
    unsigned int i;
    if (g->boss.active && g->boss.y > 0) tx = g->boss.x;
    else {
        for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
            if (g->enemies[i].active && g->enemies[i].y >= 0) {
                tx = g->enemies[i].x; break;
            }
        }
    }
    tx += xpart((int)(g->tick * 2u), 26000);
    ty += ypart((int)g->tick, 14000);
    for (i = 0; i < GARDEN_MAX_ENEMY_BULLETS; ++i) {
        const GardenEnemyBullet *b = &g->enemy_bullets[i];
        int32_t dx, dy;
        if (!b->active || b->cord_wait >= 0) continue;
        dx = g->player.x - (b->x + b->vx * 6);
        dy = g->player.y - (b->y + b->vy * 6);
        if (abs32(dx)<100000 && abs32(dy)<100000) near_danger=1U;
        if (abs32(dx) < 36000 && abs32(dy) < 42000) {
            repel_x += dx >= 0 ? 4500 : -4500;
            repel_y += dy >= 0 ? 3500 : -3500;
            /* Reuse this actual activated-projectile scan, not the previous
             * frame's global count. An automatic bomb responds only to a
             * nearby approaching shot whose150ms forecast enters the same
             * avoidance region, while the player is vulnerable. The manual
             * one-step bomb input retains its independent immediate policy.
             */
            int32_t now_x=g->player.x-b->x,now_y=g->player.y-b->y;
            if (g->mode==GARDEN_MODE_BOSS && b->ttl>1U && !g->player.grace &&
                abs32(now_x)<100000 && abs32(now_y)<100000 &&
                abs32(dx)+abs32(dy)<abs32(now_x)+abs32(now_y)) bomb_danger=1U;
        }
    }
    /* The demo really approaches rewards, only while its near-field forecast
     * is clear. Attraction still begins at the normal96px distance. Avoid
     * chasing into the upper enemy-entrance zone or an occupied target body.
     */
    if (!near_danger) {
        int32_t nearest=INT32_MAX;
        for (i=0U;i<GARDEN_MAX_PICKUPS;i++) {
            const GardenPickup *p=&g->pickups[i];
            if (!p->active || p->age<20U || p->y<200000 || p->magnet) continue;
            unsigned safe=1U;
            for (unsigned j=0U;j<GARDEN_MAX_ENEMIES;j++)
                if (g->enemies[j].active && abs32(g->enemies[j].x-p->x)<80000 &&
                    abs32(g->enemies[j].y-p->y)<100000) safe=0U;
            if (g->boss.active && abs32(g->boss.x-p->x)<110000 &&
                abs32(g->boss.y-p->y)<100000) safe=0U;
            int32_t distance=abs32(g->player.x-p->x)+abs32(g->player.y-p->y);
            if (safe && distance<nearest) { nearest=distance; tx=p->x; ty=p->y; }
        }
    }
    *vx = clamp32((tx - g->player.x) / 12 + repel_x, -6000, 6000);
    *vy = clamp32((ty - g->player.y) / 12 + repel_y, -6000, 6000);
    return bomb_danger;
}

static unsigned run_player(GardenGame *g, uint16_t buttons)
{
    int32_t vx = 0, vy = 0;
    unsigned bomb_danger=0U;
    if (!g->player.active) return 0U;
    if (g->player.grace) --g->player.grace;
    if (g->player.respawning) {
        --g->player.respawning; g->player.y -= 4000;
        return 0U;
    }
    if (g->demo_mode) bomb_danger=auto_pilot(g, &vx, &vy);
    else {
        int dx = !!(buttons & GARDEN_INPUT_RIGHT) - !!(buttons & GARDEN_INPUT_LEFT);
        int dy = !!(buttons & GARDEN_INPUT_DOWN) - !!(buttons & GARDEN_INPUT_UP);
        /* input.c:370-474, including 1414*3 diagonal and half-speed slow. */
        int32_t speed = dx && dy ? 4242 : 6000;
        if (buttons & GARDEN_INPUT_SLOW) speed /= 2;
        vx = dx * speed; vy = dy * speed;
    }
    g->player.move_x = (int8_t)(vx > 0 ? 1 : (vx < 0 ? -1 : 0));
    g->player.move_y = (int8_t)(vy > 0 ? 1 : (vy < 0 ? -1 : 0));
    g->player.x = clamp32(g->player.x + vx, 5000, 635000);
    g->player.y = clamp32(g->player.y + vy, 5000, 475000);
    fire_player(g);
    return bomb_danger;
}

static void check_player_collisions(GardenGame *g)
{
    unsigned int i;
    if (!g->player.active || g->player.grace || g->player.respawning) return;
    /* Explicit 9px square hit area, following the unused reference routine
     * ebullet.c:384-397, instead of upstream draw/check_pixel side effects.
     */
    for (i = 0; i < GARDEN_MAX_ENEMY_BULLETS; ++i) {
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        if (b->active && b->cord_wait < 0 &&
            overlaps(b->x, b->y, g->player.x, g->player.y, 9000, 9000)) {
            b->active = 0; damage_player(g); return;
        }
    }
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i) {
        GardenEnemy *e = &g->enemies[i];
        int32_t rx = e->type == GARDEN_ENEMY_DARTER ? 27000 : 47000;
        int32_t ry = e->type == GARDEN_ENEMY_DARTER ? 27000 : 45000;
        if (e->active && overlaps(e->x, e->y, g->player.x, g->player.y, rx, ry)) {
            damage_player(g); return;
        }
    }
    if (g->boss.active &&
        overlaps(g->boss.x, g->boss.y, g->player.x, g->player.y, 80000, 42000))
        damage_player(g);
}

static void check_grazes(GardenGame *g)
{
    if (!g->player.active || g->player.grace || g->player.respawning ||
        (g->mode!=GARDEN_MODE_WAVES && g->mode!=GARDEN_MODE_BOSS)) return;
    for (unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++) {
        GardenEnemyBullet *b=&g->enemy_bullets[i];
        if (!b->active || b->cord_wait>=0 || b->grazed ||
            !overlaps(b->x,b->y,g->player.x,g->player.y,28000,28000) ||
             overlaps(b->x,b->y,g->player.x,g->player.y,9000,9000)) continue;
        b->grazed=1U;
        g->reward.graze_count=combat_add(g->reward.graze_count,1U);
        g->reward.graze_score_total=combat_add(g->reward.graze_score_total,
                                                 reward_score(g,GARDEN_GRAZE_SCORE));
    }
}

static void update_counts(GardenGame *g)
{
    unsigned int i;
    g->enemy_count = g->player_bullet_count = g->enemy_bullet_count = 0;
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i)
        if (g->enemies[i].active) ++g->enemy_count;
    for (i = 0; i < GARDEN_MAX_PLAYER_BULLETS; ++i)
        if (g->player_bullets[i].active) ++g->player_bullet_count;
    for (i = 0; i < GARDEN_MAX_ENEMY_BULLETS; ++i)
        if (g->enemy_bullets[i].active) ++g->enemy_bullet_count;
    if (g->enemy_bullet_count > g->enemy_bullet_peak)
        g->enemy_bullet_peak = g->enemy_bullet_count;
}

void garden_init(GardenGame *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->initial_seed = seed ? seed : UINT32_C(0x47617264);
    g->rng = g->initial_seed;
    g->lives = 3; g->demo_mode = 1; g->mode = GARDEN_MODE_WAVES;
    g->player.active = 1; g->player.x = 320000; g->player.y = 410000;
    g->player.grace = 80;
    g->reward.bombs=2U;
}

static void restart_game(GardenGame *g)
{
    uint32_t best=g->reward.high_score>g->score ? g->reward.high_score : g->score;
    uint32_t seed=g->initial_seed;
    garden_init(g,seed);
    g->reward.high_score=best;
}

void garden_step(GardenGame *g, uint16_t buttons)
{
    /* Paused/end-state/restart calls cannot replay a previous hit batch. */
    g->combat.count=0U;
    g->combat.tick=g->tick;
    unsigned int i;
    uint16_t directions = GARDEN_INPUT_LEFT | GARDEN_INPUT_RIGHT |
                          GARDEN_INPUT_UP | GARDEN_INPUT_DOWN;
    if (buttons & GARDEN_INPUT_RESTART) {
        restart_game(g); return;
    }
    if (buttons & GARDEN_INPUT_PAUSE) g->paused ^= 1u;
    if (buttons & GARDEN_INPUT_TOGGLE_DEMO) g->demo_mode ^= 1u;
    if (buttons & directions) g->demo_mode = 0;
    if (g->paused) return;
    ++g->tick;
    g->combat.tick=g->tick;
    if (g->reward.pickup_flash_ticks) --g->reward.pickup_flash_ticks;
    if (g->reward.bomb_flash_ticks) --g->reward.bomb_flash_ticks;
    for (i = 0; i < GARDEN_MAX_EFFECTS; ++i) {
        GardenEffect *f = &g->effects[i];
        if (f->active && ++f->age >= 20) f->active = 0;
    }
    if (g->mode == GARDEN_MODE_GAMEOVER || g->mode == GARDEN_MODE_VICTORY) {
        if (g->end_ticks<UINT16_MAX) ++g->end_ticks;
        run_pickups(g);
        if (g->demo_mode && g->end_ticks >= GARDEN_END_TICKS) {
            restart_game(g); return;
        }
        update_counts(g); return;
    }
    g->reward.run_ticks=combat_add(g->reward.run_ticks,1U);
    unsigned can_bomb=g->player.active && !g->player.respawning;
    unsigned demo_bomb_danger=run_player(g, buttons);
    if (g->mode == GARDEN_MODE_WAVES) run_waves(g);
    run_enemies(g);
    if (g->mode == GARDEN_MODE_BOSS) run_boss(g);
    run_enemy_bullets(g);
    if (can_bomb && ((buttons&GARDEN_INPUT_BOMB) ||
        (g->demo_mode && g->mode==GARDEN_MODE_BOSS &&
         !g->player.grace && !g->reward.bombs_used && demo_bomb_danger))) fire_bomb(g);
    run_player_bullets(g);
    check_player_collisions(g);
    check_grazes(g);
    run_pickups(g);
    update_counts(g);
}
