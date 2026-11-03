/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reduced Garden of Coloured Lights 1.0.9 game core for Ti60.
 * Original game: Linley Henzell; upstream distribution: garden.sourceforge.net.
 * See ../vendor/garden-1.0.9/COPYING for the GNU GPL version 3 or later.
 *
 * This is a MODIFIED, PARTIAL PORT, not the complete original game:
 * - Stage 1's DARTER1 / STOPPER1 content is shortened to two groups and Boss 1.
 * - Boss 1's three original attack generators are retained, on easy difficulty.
 *   This variety edition chooses each attack from a shuffled three-item bag,
 *   including the twin-laser attack, with no adjacent repeat across bags.
 *   Wave enemy type fixes style/colour; firing delays remain bounded random;
 *   Boss timing is drawn once per train, never redrawn during its telegraph.
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
 * - Rewards add one graze per activated bullet (+5), real falling/magnetic
 *   pickups (+25), an eight-second end state and a runtime-only high score.
 * - This ordnance edition adds two finite600-damage physical bombs (no bullet
 *   clear or bonus invulnerability), STOPPER healing drops capped at3lives,
 *   telegraphed twin lasers and exact swept collisions with the original
 *   player hit square. The automatic pilot uses those same finite resources.
 * - This hazard edition replaces long invisible cord-based immunity with a
 *   uniform6tick150ms forming cue. From age6 every normal bullet is armed;
 *   original player grace/respawn protection remains intact.
 */
#include "garden_core.h"
#include "garden_clear.h"
#include <string.h>

/* CPU scan/admission extent; physical DDR arrays remain2000 slots. */
unsigned garden_enemy_scan_limit=192U;

/* This isolated edition stores the complete game in its guarded DDR reservation. */
typedef char garden_state_must_fit_g9_ddr[
    sizeof(GardenGame) <= 0x18000u ? 1 : -1];

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

static void drop_pickup(GardenGame *g,int32_t x,int32_t y,unsigned kind)
{
    for (unsigned i=0U;i<GARDEN_MAX_PICKUPS;i++) {
        GardenPickup *p=&g->pickups[i];
        if (p->active) continue;
        *p=(GardenPickup){.x=clamp32(x,5000,635000),
            .y=clamp32(y,5000,460000),.vx=(i&1U) ? 600 : -600,
            .vy=-1800,.active=1U,.kind=(uint8_t)kind};
        g->reward.items_spawned=combat_add(g->reward.items_spawned,1U);
        if(kind==GARDEN_PICKUP_HEAL)g->weapon_stats.heals_spawned=combat_add(g->weapon_stats.heals_spawned,1U);
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
            if(p->kind==GARDEN_PICKUP_HEAL){
                g->weapon_stats.heals_collected=combat_add(g->weapon_stats.heals_collected,1U);
                if(g->lives<3U){g->lives++;g->weapon_stats.heals_restored=combat_add(g->weapon_stats.heals_restored,1U);}
            }
            g->reward.item_score_total=combat_add(g->reward.item_score_total,
                                                   reward_score(g,GARDEN_PICKUP_SCORE));
            g->reward.pickup_flash_x=g->player.x;
            g->reward.pickup_flash_y=g->player.y;
            g->reward.pickup_flash_ticks=16U;
            g->reward.pickup_flash_kind=p->kind;
        } else if (p->age>=480U || p->y>510000) p->active=0U;
        count+=p->active!=0U;
    }
    g->reward.pickup_count=(uint8_t)count;
}

static void fire_bomb(GardenGame *g)
{
    if (!g->reward.bombs || g->ordnance.active || !g->player.active || g->player.respawning ||
        (g->mode!=GARDEN_MODE_WAVES && g->mode!=GARDEN_MODE_BOSS)) return;
    g->ordnance=(GardenBomb){.x=g->player.x,.y=g->player.y-16000,.active=1U};
    --g->reward.bombs;
    g->reward.bombs_used=combat_add(g->reward.bombs_used,1U);
    g->weapon_stats.bombs_launched=combat_add(g->weapon_stats.bombs_launched,1U);
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

static uint32_t seed_successor(uint32_t x)
{
    /* xorshift32 has a 2^32-1 nonzero-state period. A run's successor is
     * independent of how long it lasted: fixed first seed + input trace
     * reproduces every restart, while restarting immediately never repeats. */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

static uint32_t random_u32(GardenGame *g)
{ return g->rng=seed_successor(g->rng); }
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
         i < garden_enemy_scan_limit; ++i) {
#ifdef GARDEN_HOST_ALLOC_PROBES
        garden_host_alloc_probe(g->boss_alloc_enabled!=0U,i);
#endif
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        if (!b->active) {
            if (g->boss_alloc_enabled) g->boss_alloc_cursor=(uint16_t)(i+1U);
            memset(b, 0, sizeof(*b));
            b->active = 1; b->type = type;
            b->x = x; b->y = y;
            b->angle = b->cord_angle = (uint16_t)(angle & 1023);
            b->colour = (uint8_t)colour;
            (void)wait; /* Legacy generator argument; W uses uniform forming. */
            b->cord_wait = GARDEN_BULLET_ARM_TICKS;
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
 * 32 pairs, 32/16-unit angle offsets and acceleration. The adapted cruiser
 * emits from its actual lower nose instead of the former centre/radius.
 */
static void boss_attack1(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    int i, angle = 0, a1 = 0, xa = 8, accel1, accel2;
    /* Retain the former colour RNG draws so visual grouping cannot change
     * the seeded attack timing, projectile trajectories or pilot outcome. */
    (void)grand(g,5U); (void)grand(g,4U);
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
        fire_dart(g, b->x,b->y+GARDEN_BOSS_NOSE_Y_PX*GARDEN_GRAIN,
                  angle, b->volley_colour, 80, accel1);
        fire_dart(g, b->x,b->y+GARDEN_BOSS_NOSE_Y_PX*GARDEN_GRAIN,
                  angle + 16, b->volley_colour, 100, accel2);
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
    (void)grand(g,5U); /* Preserve the existing seeded schedule. */
    for (i = 0; i < 3; ++i) {
        for (k = 0; k < 3; ++k)
            fire_dart(g, b->x,b->y+GARDEN_BOSS_NOSE_Y_PX*GARDEN_GRAIN,
                      angle + k * 341, b->volley_colour, 80, 60 + i * 20);
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
    (void)grand(g,5U); /* Preserve the existing seeded schedule. */
    for (i = 0; i < 12; ++i) {
        int32_t x = b->x;
        int32_t y = b->y+GARDEN_BOSS_NOSE_Y_PX*GARDEN_GRAIN;
        fire_diamond(g, x, y, angle, b->volley_colour, 2200 + xpart(i * 21, 600),
                     xpart(i * 21, 8), 80);
        fire_diamond(g, x, y, angle + 1024 / 21, b->volley_colour,
                     2000 + xpart(i * 21, 600),
                     xpart(i * 21 + 512, 12), 40);
        fire_diamond(g, x, y, angle + 1024 / 21, b->volley_colour,
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
    for (unsigned i=0U;i<garden_enemy_scan_limit;i++)
        count+=g->enemy_bullets[i].active==0U;
    unsigned active=garden_enemy_scan_limit-count;
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

static void laser_start(GardenGame *g)
{
    g->laser=(GardenLaser){.x={g->boss.x+GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN,
                                      g->boss.x+GARDEN_BOSS_MUZZLE_RIGHT_X_PX*GARDEN_GRAIN},
        .y=g->boss.y+GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN,.ticks=GARDEN_LASER_WARN_TICKS,.phase=GARDEN_LASER_WARN,
        .cycle=(uint8_t)(g->laser.cycle+1U)};
    g->weapon_stats.laser_warnings=combat_add(g->weapon_stats.laser_warnings,1U);
}

static void run_laser(GardenGame *g)
{
    GardenLaser *l=&g->laser;
    if(!g->boss.active){l->phase=GARDEN_LASER_OFF;l->ticks=0U;return;}
    if(l->phase==GARDEN_LASER_OFF)return;
    if(l->ticks)--l->ticks;
    if(l->ticks)return;
    if(l->phase==GARDEN_LASER_WARN){
        l->phase=GARDEN_LASER_FIRE;l->ticks=GARDEN_LASER_FIRE_TICKS;
        g->weapon_stats.laser_shots=combat_add(g->weapon_stats.laser_shots,1U);
    }else if(l->phase==GARDEN_LASER_FIRE){l->phase=GARDEN_LASER_FADE;l->ticks=GARDEN_LASER_FADE_TICKS;}
    else l->phase=GARDEN_LASER_OFF;
}

unsigned garden_boss_fire_countdown(const GardenGame *g)
{
    if (!g || g->mode!=GARDEN_MODE_BOSS || !g->boss.active ||
        g->boss.rhythm_state!=GARDEN_BOSS_CHARGE ||
        !g->boss.burst_left || !g->boss.rhythm_ticks || g->boss.rhythm_ticks>20U ||
        !boss_has_train_space(g)) return 0U;
    return g->boss.rhythm_ticks;
}

static void boss_choose_attack(GardenGame *g)
{
    GardenBoss *b=&g->boss;
    unsigned mask=b->attack_bag,choices,count=0U;
    if(!mask){
        mask=7U;choices=mask;
        /* A new bag contains all three attacks, but may not start by
         * repeating the last attack of the preceding complete bag. */
        if(b->attack>=1U && b->attack<=3U)choices&=~(1U<<(b->attack-1U));
    }else choices=mask;
    for(unsigned i=0U;i<3U;i++)count+=(choices>>i)&1U;
    unsigned pick=(unsigned)grand(g,count);
    for(unsigned i=0U;i<3U;i++)if(choices&(1U<<i)){
        if(!pick){b->attack=(uint8_t)(i+1U);b->attack_bag=(uint8_t)(mask&~(1U<<i));break;}
        --pick;
    }
    b->status=(uint8_t)grand(g,4U);
}

static void boss_schedule_charge(GardenGame *g,unsigned fresh_train)
{
    GardenBoss *b=&g->boss;
    if(fresh_train){
        unsigned band=b->max_hp>0 && (int64_t)b->hp*100<= (int64_t)b->max_hp*40 ? 2U :
                      b->max_hp>0 && (int64_t)b->hp*100<= (int64_t)b->max_hp*70 ? 1U : 0U;
        if (b->hp_band!=band) g->rhythm.hp_band_changes++;
        b->hp_band=(uint8_t)band;
        /* Randomize once at the safe segment boundary. The former minimum
         * warning/rest/gap is retained at every HP band. Capacity WAIT keeps
         * these exact values and the announced type until this train ends. */
        b->charge_ticks=(uint16_t)(20U-4U*band+(unsigned)grand(g,9U));
        b->rest_ticks=(uint16_t)(80U-16U*band+(unsigned)grand(g,25U));
        b->burst_gap=(uint8_t)(b->attack==2U ? 6U-band+(unsigned)grand(g,3U) :
                                                          12U-2U*band+(unsigned)grand(g,5U));
        /* One bank for this whole announced attack, including every speed
         * lane, ring, pulse and any capacity WAIT. Reading the current PRNG
         * state preserves the existing mechanical random sequence. */
        b->volley_colour=(uint8_t)(1U+g->rng%5U);
    }
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
    /* Capacity WAIT may defer the very first charge. burst_left==6 identifies
     * that unstarted attack2 train even when its later admission is fresh=0.
     */
    if(b->attack==2U && b->burst_left==6U && g->laser.phase==GARDEN_LASER_OFF)laser_start(g);
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

static unsigned wave_enemies_alive(const GardenGame *g)
{
    unsigned alive=0U;
    for(unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++)alive+=g->enemies[i].active!=0U;
    return alive;
}

static void start_boss(GardenGame *g)
{
    /* A transition is never allowed to delete a still-active wave target.
     * Scan actual slots: enemy_count can still describe the preceding step. */
    if(g->wave_next!=GARDEN_WAVE_ENEMIES || g->wave_kills!=GARDEN_WAVE_ENEMIES || wave_enemies_alive(g))return;
    /* Cut to Boss after the shortened waves; discard leftover wave bullets. */
    /* The diagnostic driver changes the scan extent only through an init.
     * Unscanned upper slots therefore remain zero; avoid writing all2000
     * DDR slots when the natural game only admits/scans192. */
    unsigned clear_slots=garden_enemy_scan_limit;
    if(clear_slots>GARDEN_MAX_ENEMY_BULLETS)clear_slots=GARDEN_MAX_ENEMY_BULLETS;
    garden_clear_zero(g->enemy_bullets,clear_slots*sizeof(g->enemy_bullets[0]));
    garden_clear_zero(&g->boss,sizeof(g->boss));
    g->boss.active = 1; g->boss.x = 320000; g->boss.y = -80000;
    g->boss.target_x = g->boss.x;
    g->boss.vy = 3000; g->boss.move_ticks = 100;
    /* level.c:1617-1633 original Boss 1 HP and 100-tick entrance. */
    g->boss.hp = g->boss.max_hp = 50000;
    boss_choose_attack(g);
    g->boss.rhythm_state=GARDEN_BOSS_TRANSIT;
    g->mode = GARDEN_MODE_BOSS;
}

static void run_boss(GardenGame *g)
{
    GardenBoss *b = &g->boss;
    if (!b->active) return;
    unsigned anchored=g->laser.phase!=GARDEN_LASER_OFF;
    if(anchored){
        /* Fixed warning geometry is also the body's firing position. Keep
         * rhythm timers running while the mount stands still to fire. */
        /* Averaging asymmetric muzzles would place the ship0.5px off its
         * anchor. Invert the exact left mount instead, without rounding. */
        b->x=g->laser.x[0]-GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN;
        b->y=g->laser.y-GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN;b->vx=0;b->vy=0;
    }
    if (b->flash) --b->flash;
    if (b->move_ticks) {
        --b->move_ticks;
        if (!anchored && b->y < 120000) b->y += b->vy;
        /* Demonstration transit: simpler than original fold/drag/target AI. */
        if (!anchored) b->x += (b->target_x - b->x) / 20;
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
        boss_choose_attack(g);
        b->move_ticks=(uint16_t)(32U+(unsigned)grand(g,25U)); b->rhythm_state=GARDEN_BOSS_TRANSIT;
        b->target_x=150000+grand(g,390000);
        /* Keep every setup interval and PRNG draw. Only each third complete
         * attack group changes position; the other two hold here. */
        if(g->rhythm.bursts_completed%3U) b->target_x=b->x;
    }
}

static unsigned spawn_enemy(GardenGame *g, uint8_t type, int32_t x, int32_t y)
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
            return 1U;
        }
    }
    return 0U;
}

/* Stage 1 selection: level.c:292 (STOPPER1 + DARTER1), 295 (STOPPER1),
 * 319 (Boss 1); group of four with delay 80 from level.c:1069-1097.
 * Total lead-in is intentionally shortened; not the original stage schedule.
 */
static void run_waves(GardenGame *g)
{
    /* Exactly ten ordered tasks. A full eight-slot pool defers the next
     * delivery without RNG draws, lost tasks or duplicate spawns. */
    while(g->wave_next<GARDEN_WAVE_ENEMIES){
        unsigned n=g->wave_next;
        unsigned due=n<2U ? 40U : n<5U ? 40U+(n-1U)*80U : n<9U ? 400U+(n-5U)*80U : 680U;
        if(g->tick<due || wave_enemies_alive(g)>=GARDEN_MAX_ENEMIES)break;
        unsigned ok;
        if(n==1U || n==9U)
            ok=spawn_enemy(g,GARDEN_ENEMY_STOPPER,220000+grand(g,200000),-30000);
        else{
            unsigned lane=n==0U ? 0U:n<5U ? n-1U:n-5U;
            ok=spawn_enemy(g,GARDEN_ENEMY_DARTER,100000+(int32_t)lane*120000,-30000-grand(g,140000));
        }
        if(!ok)break;
        g->wave_next++;
    }
    if(g->tick>=1000U && g->wave_next==GARDEN_WAVE_ENEMIES &&
       g->wave_kills==GARDEN_WAVE_ENEMIES && !wave_enemies_alive(g))start_boss(g);
}

/* Lower-density wave STOPPER: three alternating-winding sets.
 * Acceleration and collision behavior stay unchanged; Boss is independent.
 */
static void stopper_fire(GardenGame *g, const GardenEnemy *e, unsigned colour)
{
    int i, a = grand(g, 1024), inc = 1024 / 30;
    if (grand(g, 2) == 0) inc = -inc;
    for (i = 0; i < 3; ++i) {
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, (int)colour, 50, 120); a += inc;
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, (int)colour, 50, 100); a += inc;
        fire_dart(g, e->x + xpart(a, 24000), e->y + ypart(a, 24000),
                  a, (int)colour, 50, 80); a += inc * 2;
    }
}

/* One fixed projectile family, colour and pattern per wave enemy type.
 * Firing delays remain bounded random; a ring may rotate its eight spokes.
 * Boss generators and the normal collision/arming rules are independent. */
static void enemy_fire(GardenGame *g,GardenEnemy *e)
{
    int aim=e->aim_angle;
    if(e->type==GARDEN_ENEMY_DARTER){
        int32_t x=e->x+xpart(aim,7000),y=e->y+ypart(aim,7000);
        for(int n=-1;n<=1;n++)
            fire_dart(g,x,y,aim+n*32,1,50,70);
        e->recycle=(uint16_t)(36U+(unsigned)grand(g,25U));
    }else{
        int start=grand(g,1024U);
        for(unsigned n=0U;n<8U;n++){
            int angle=start+(int)(n*1024U/8U);
            fire_diamond(g,e->x+xpart(angle,24000),e->y+ypart(angle,24000),
                         angle,4,1700,0,50);
        }
        e->recycle=(uint16_t)(120U+(unsigned)grand(g,41U));
    }
}

/* Phase4 follows a40tick smoothstep inside the upper combat region.
 * It has no coasting tail or lifetime escape: a live target stays on stage.
 * vx/vy remain actual step velocities for the pilot's avoidance forecast. */
static void run_enemies(GardenGame *g)
{
    for (unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++) {
        GardenEnemy *e=&g->enemies[i];
        if(!e->active)continue;
        if(e->age<UINT16_MAX)++e->age;
        if(e->flash)--e->flash;
        if(e->recycle)--e->recycle;
        --e->target_time;
        if(e->target_time<=0){
            if(e->phase==0U){
                e->phase=1U;e->target_time=e->type==GARDEN_ENEMY_DARTER ? 30:60;
            }else if(e->phase==1U){
                e->phase=2U;e->vx=e->vy=0;
                e->target_time=e->type==GARDEN_ENEMY_DARTER ? 90:150;
                e->recycle=(uint16_t)(e->type==GARDEN_ENEMY_DARTER ? 8U+(unsigned)grand(g,17U):
                                                                               40U+(unsigned)grand(g,25U));
            }else if(e->phase==2U){
                e->phase=3U;e->target_time=e->type==GARDEN_ENEMY_DARTER ? 30:60;
            }else if(e->phase==3U){
                e->phase=4U;e->target_time=40;
                if(e->moves<UINT8_MAX)++e->moves;
                /* Alternate screen halves, staying above the player's region.
                 * Every trip has at least40px horizontal travel. */
                e->target_x=(e->x<320000 ? 360000:100000)+grand(g,180001);
                e->target_y=90000+grand(g,160001);
                e->vx=e->vy=0;
            }
        }
        e->aim_angle=turn_towards(e->aim_angle,
            aim_at(g->player.x-e->x,g->player.y-e->y),8);
        if(e->phase==4U){
            /* Allocate the remaining displacement by the remaining cubic
             * weight. Last step has weight==remaining, so it lands exactly.
             * Bounds <=440000 and weight <=2398 keep products in int32. */
            int32_t step=41-e->target_time;
            int32_t weight=(246-6*step)*step-122;
            int32_t prior=step-1;
            int32_t remaining=64000-(120-2*prior)*prior*prior;
            e->vx=(e->target_x-e->x)*weight/remaining;
            e->vy=(e->target_y-e->y)*weight/remaining;
            e->x+=e->vx;e->y+=e->vy;
            if(e->target_time==1){
                e->x=e->target_x;e->y=e->target_y;
                e->vx=e->vy=0;e->phase=1U;e->target_time=30;
            }
        }else{
            if(e->phase==1U || e->phase==3U){
                e->vx=e->vx*910/1000;e->vy=e->vy*910/1000;
            }else if(e->phase==2U && !e->recycle)enemy_fire(g,e);
            e->x+=e->vx;e->y+=e->vy;
        }
    }
}

/* Original bullet movement, with a visible uniform forming period replacing
 * the port's long cord catch-up immunity. No invisible post-forming gate.
 */
static void run_enemy_bullets(GardenGame *g)
{
    unsigned int i;
    for (i = 0; i < garden_enemy_scan_limit; ++i) {
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        if (!b->active) continue;
        /* bit0 is the lifetime graze flag; bit1 marks activation at this
         * endpoint only, so an unarmed trajectory is never swept as damage.
         */
        b->grazed&=1U;
        ++b->age;
        if (b->type == GARDEN_BULLET_DART && b->age < 60) {
            b->vx += xpart(b->angle, b->accel);
            b->vy += ypart(b->angle, b->accel);
        }
        b->x += b->vx; b->y += b->vy;
        if (b->type == GARDEN_BULLET_DIAMOND)
            b->angle = (uint16_t)((b->angle + b->spin) & 1023);
        b->cord_wait=b->age>=GARDEN_BULLET_ARM_TICKS ? -1 :
                     (int16_t)(GARDEN_BULLET_ARM_TICKS-b->age);
        if(b->age==GARDEN_BULLET_ARM_TICKS)b->grazed|=2U;
        if (b->ttl) --b->ttl;
        /* Lifetime expiry stays immediate; spatial culling happens in the
         * existing count pass after swept collision. A fast outgoing segment
         * must not disappear before its interior player crossing is tested.
         */
        if (!b->ttl) b->active = 0;
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
    g->laser.phase=GARDEN_LASER_OFF;g->laser.ticks=0U;
    g->reward.hits_taken=combat_add(g->reward.hits_taken,1U);
    add_effect(g, g->player.x, g->player.y, 1);
    if (g->lives) --g->lives;
    /* This presentation build settles each player death immediately. Its
     * 15-second no-input restart is a wall-clock application policy, never
     * a number of simulated ticks or a side effect of automatic steering. */
    g->ordnance.active=0U;g->ordnance.blast_ticks=0U;
    g->player.active=0U;g->mode=GARDEN_MODE_GAMEOVER;g->end_ticks=0U;
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

static unsigned hit_target(GardenGame *g,const GardenPlayerBullet *shot,unsigned slot)
{
    int32_t before,x,y;unsigned type,killed,reward=0U;
    if(slot<GARDEN_MAX_ENEMIES){
        GardenEnemy *e=&g->enemies[slot];
        if(!e->active || e->hp<=0)return 0U;
        before=e->hp;x=e->x;y=e->y;type=e->type;
        e->hp-=shot->damage;e->flash=3U;killed=e->hp<=0;
        if(killed){
            add_effect(g,x,y,type==GARDEN_ENEMY_STOPPER);
            drop_pickup(g,x,y,type==GARDEN_ENEMY_STOPPER ? GARDEN_PICKUP_HEAL:GARDEN_PICKUP_CORE);
            reward=type==GARDEN_ENEMY_DARTER ? 20U:80U;e->active=0U;
            if(g->mode==GARDEN_MODE_WAVES && g->wave_kills<GARDEN_WAVE_ENEMIES)g->wave_kills++;
        }
    }else{
        GardenBoss *b=&g->boss;
        if(!b->active || b->hp<=0)return 0U;
        before=b->hp;x=b->x;y=b->y;type=GARDEN_ENEMY_NONE;
        b->hp-=shot->damage;b->flash=3U;killed=b->hp<=0;
        if(killed){b->hp=0;b->active=0U;reward=1000U;add_effect(g,x,y,1U);drop_pickup(g,x,y,GARDEN_PICKUP_CORE);}
    }
    combat_reward(g,shot,before,x,y,slot,type,slot==GARDEN_MAX_ENEMIES,killed,reward);
    if(killed && slot==GARDEN_MAX_ENEMIES){
        /* The pressure driver changes the scan extent only through a full
         * public init, so all slots above this prefix are already zero. */
        unsigned clear_slots=garden_enemy_scan_limit;
        if(clear_slots>GARDEN_MAX_ENEMY_BULLETS)clear_slots=GARDEN_MAX_ENEMY_BULLETS;
        garden_clear_zero(g->enemy_bullets,clear_slots*sizeof(g->enemy_bullets[0]));
        garden_clear_zero(g->player_bullets,sizeof(g->player_bullets));
        garden_clear_zero(g->enemies,sizeof(g->enemies));
        g->player_bullet_count=g->enemy_bullet_count=g->enemy_count=0U;
        g->laser.phase=GARDEN_LASER_OFF;g->laser.ticks=0U;
        if(!g->ordnance.blast_ticks)g->ordnance.active=0U;
        g->mode=GARDEN_MODE_VICTORY;g->end_ticks=0U;
    }
    return (unsigned)before<shot->damage ? (unsigned)before:shot->damage;
}

static unsigned within_blast(int32_t x,int32_t y,int32_t cx,int32_t cy)
{
    int32_t dx=x-cx,dy=y-cy;
    return abs32(dx)<=GARDEN_BOMB_RADIUS && abs32(dy)<=GARDEN_BOMB_RADIUS &&
           (int64_t)dx*dx+(int64_t)dy*dy<=(int64_t)GARDEN_BOMB_RADIUS*GARDEN_BOMB_RADIUS;
}

static void run_ordnance(GardenGame *g)
{
    GardenBomb *o=&g->ordnance;
    if(!o->active)return;
    if(o->blast_ticks){if(!--o->blast_ticks)o->active=0U;return;}
    o->age++;o->y-=9000;
    if(o->age>=80U || o->y<-20000){o->active=0U;return;}
    unsigned fuse=0U;
    for(unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++)
        if(g->enemies[i].active && overlaps(o->x,o->y,g->enemies[i].x,g->enemies[i].y,48000,48000))fuse=1U;
    if(g->boss.active && g->boss.y>=0 && overlaps(o->x,o->y,g->boss.x,g->boss.y,48000,48000))fuse=1U;
    if(!fuse)return;
    o->blast_ticks=GARDEN_BOMB_BLAST_TICKS;
    GardenPlayerBullet blast={.x=o->x,.y=o->y,.damage=GARDEN_BOMB_DAMAGE};
    unsigned targets=0U,damage=0U;
    for(unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++)
        if(g->enemies[i].active && within_blast(g->enemies[i].x,g->enemies[i].y,o->x,o->y)){
            unsigned actual=hit_target(g,&blast,i);targets+=actual!=0U;damage+=actual;
        }
    if(g->boss.active && within_blast(g->boss.x,g->boss.y,o->x,o->y)){
        unsigned actual=hit_target(g,&blast,GARDEN_MAX_ENEMIES);targets+=actual!=0U;damage+=actual;
    }
    if(targets)g->weapon_stats.bomb_impacts=combat_add(g->weapon_stats.bomb_impacts,1U);
    g->weapon_stats.bomb_targets=combat_add(g->weapon_stats.bomb_targets,targets);
    g->weapon_stats.bomb_damage=combat_add(g->weapon_stats.bomb_damage,damage);
}

static unsigned auto_throw(const GardenGame *g)
{
    if(!g->demo_mode || !g->reward.bombs || g->ordnance.active)return 0U;
    if(g->mode==GARDEN_MODE_WAVES && g->reward.bombs>=2U){
        for(unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++){
            const GardenEnemy *e=&g->enemies[i];
            if(e->active && e->type==GARDEN_ENEMY_STOPPER && e->hp>0 && e->y>0 &&
                g->player.y-e->y>40000 && abs32(g->player.x-e->x)<60000)return 1U;
        }
    }
    return g->mode==GARDEN_MODE_BOSS && g->boss.active && g->boss.hp>0 &&
           g->boss.y>=0 && g->boss.attack==2U && g->boss.rhythm_state==GARDEN_BOSS_CHARGE &&
           g->player.y-g->boss.y>40000 && abs32(g->player.x-g->boss.x)<60000;
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
                hit_target(g,b,j);b->active=0U;
                break;
            }
        }
        if (b->active && g->boss.active && g->boss.y >= 0 &&
            overlaps(b->x, b->y, g->boss.x, g->boss.y, 76000, 42000)) {
            hit_target(g,b,GARDEN_MAX_ENEMIES);b->active=0U;
            if(g->mode==GARDEN_MODE_VICTORY)break;
        }
        if (b->y < -30000 || b->y > 510000 || b->age > 90) b->active = 0;
    }
}

/* Demonstration addition. Tracks a target, slowly sweeps, and repels nearby
 * active bullets. It has no immunity, teleport or scripted score/HP changes.
 */
static void auto_avoid(const GardenGame *g,int32_t *vx,int32_t *vy);
static void auto_pilot(GardenGame *g, int32_t *vx, int32_t *vy)
{
    int32_t tx = 320000, ty = 405000, repel_x = 0, repel_y = 0;
    unsigned near_danger=0U;
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
    for (i = 0; i < garden_enemy_scan_limit; ++i) {
        const GardenEnemyBullet *b = &g->enemy_bullets[i];
        int32_t dx, dy;
        if (!garden_enemy_bullet_is_armed(b)) continue;
        dx = g->player.x - (b->x + b->vx * 6);
        dy = g->player.y - (b->y + b->vy * 6);
        if (abs32(dx)<100000 && abs32(dy)<100000) near_danger=1U;
        if (abs32(dx) < 36000 && abs32(dy) < 42000) {
            repel_x += dx >= 0 ? 4500 : -4500;
            repel_y += dy >= 0 ? 3500 : -3500;
        }
    }
    if((g->laser.phase==GARDEN_LASER_WARN || g->laser.phase==GARDEN_LASER_FIRE) &&
        g->player.y>=g->laser.y-9000){
        for(unsigned i=0U;i<2U;i++)if(abs32(g->player.x-g->laser.x[i])<30000){
            repel_x+=g->player.x>=g->laser.x[i] ? 6000:-6000;near_danger=1U;
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
    auto_avoid(g,vx,vy);
}

static void run_player(GardenGame *g, uint16_t buttons)
{
    int32_t vx = 0, vy = 0;
    if (!g->player.active) return;
    if (g->player.grace) --g->player.grace;
    if (g->player.respawning) {
        --g->player.respawning; g->player.y -= 4000;
        return;
    }
    if (g->demo_mode) auto_pilot(g, &vx, &vy);
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
}

static int64_t abs64(int64_t value){return value<0 ? -value:value;}

/* Exact segment/AABB SAT, inclusive edges. Bounding rejection is32bit and
 * cheap for distant shots; only potential crossings use64bit cross products.
 * This tests the segment itself, never its entire swept bounding rectangle.
 */
static unsigned segment_box(int32_t ax,int32_t ay,int32_t bx,int32_t by,
                            int32_t x0,int32_t y0,int32_t x1,int32_t y1)
{
    if((ax<x0 && bx<x0)||(ax>x1 && bx>x1)||(ay<y0 && by<y0)||(ay>y1 && by>y1))return 0U;
    if(bx>=x0 && bx<=x1 && by>=y0 && by<=y1)return 1U;
    int64_t mx=(int64_t)ax+bx-x0-x1,my=(int64_t)ay+by-y0-y1;
    int64_t dx=(int64_t)bx-ax,dy=(int64_t)by-ay,hx=(int64_t)x1-x0,hy=(int64_t)y1-y0;
    return abs64(mx*dy-my*dx)<=hx*abs64(dy)+hy*abs64(dx);
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Behavior-preserving replacement span: bullet_forecast through auto_avoid.
 * Hoists9action-independent forecasts and acceleration; no gameplay changes.
 */
static void bullet_forecast_pair(const GardenEnemyBullet *b,unsigned t,
                                unsigned remaining,int32_t accel_x,int32_t accel_y,
                                int32_t *out_x,int32_t *out_y)
{
    unsigned n=remaining<t ? remaining:t;
    int32_t factor=(int32_t)(n*t-n*(n-1U)/2U);
    *out_x=b->x+accel_x*factor+b->vx*(int32_t)t;
    *out_y=b->y+accel_y*factor+b->vy*(int32_t)t;
}

/* The AI alone tests a fixed square centered at zero. Keep the gameplay
 * collision SAT unchanged. After the same trivial rejections as segment_box,
 * normal forecast coordinates fit +/-10,000,000. Consequently the sums and
 * differences fit +/-20,000,000; each signed 32x32 product has magnitude at
 * most4e14, their difference at most8e14, and the RHS at most1.28e12. No
 * intermediate overflows. Unusual/out-of-range inputs use the original SAT.
 * Unsigned offset checks deliberately avoid signed overflow even at INT_MIN.
 */
static inline __attribute__((always_inline)) unsigned ai_bullet_segment_box(
    int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    if((ax < -16000 && bx < -16000)||(ax > 16000 && bx > 16000)||
       (ay < -16000 && by < -16000)||(ay > 16000 && by > 16000))return 0U;
    if(bx >= -16000 && bx <= 16000 && by >= -16000 && by <= 16000)return 1U;
    if((uint32_t)ax+10000000U>20000000U || (uint32_t)ay+10000000U>20000000U ||
       (uint32_t)bx+10000000U>20000000U || (uint32_t)by+10000000U>20000000U)
        return segment_box(ax,ay,bx,by,-16000,-16000,16000,16000);
    int32_t mx=ax+bx,my=ay+by,dx=bx-ax,dy=by-ay;
    int64_t cross=(int64_t)mx*dy-(int64_t)my*dx;
    int64_t extent=INT64_C(32000)*(abs32(dx)+abs32(dy));
    return abs64(cross)<=extent;
}

static void __attribute__((optimize("O2"))) auto_avoid(const GardenGame *g,int32_t *vx,int32_t *vy)
{
    static const uint8_t horizon[4]={2U,5U,9U,14U};
    uint32_t risks[9];
    int32_t candidate_x[9],candidate_y[9];
    for(unsigned move=0U;move<9U;move++){
        int dx=(int)(move%3U)-1,dy=(int)(move/3U)-1;
        int32_t speed=dx && dy ? 4242:6000;
        int32_t cvx=dx*speed,cvy=dy*speed;
        candidate_x[move]=cvx;candidate_y[move]=cvy;
        uint32_t risk=(uint32_t)(abs32(cvx-*vx)+abs32(cvy-*vy))/100U;
        int32_t end_x=g->player.x+cvx*14,end_y=g->player.y+cvy*14;
        if(end_x<12000 || end_x>628000 || end_y<12000 || end_y>468000)risk+=80000U;
        risks[move]=risk;
    }
    for(unsigned i=0U;i<garden_enemy_scan_limit;i++){
        const GardenEnemyBullet *b=&g->enemy_bullets[i];
        if(!b->active)continue;
        int32_t rx=b->x-g->player.x,ry=b->y-g->player.y;
        if(abs32(rx)>240000 || abs32(ry)>240000)continue;
        unsigned armed_at=b->age>=GARDEN_BULLET_ARM_TICKS ? 0U:GARDEN_BULLET_ARM_TICKS-b->age;
        unsigned remaining=0U;
        int32_t accel_x=0,accel_y=0;
        if(b->type==GARDEN_BULLET_DART && b->age<59U){
            remaining=59U-b->age;
            accel_x=xpart(b->angle,b->accel);
            accel_y=ypart(b->angle,b->accel);
        }
        unsigned before=0U,cached_t=UINT32_MAX;
        int32_t cached_x=0,cached_y=0;
        for(unsigned s=0U;s<4U;s++){
            unsigned t=horizon[s],from=before;before=t;
            if(t<armed_at)continue;
            if(from<armed_at)from=armed_at;
            int32_t start_x,start_y,finish_x,finish_y;
            if(cached_t==from){start_x=cached_x;start_y=cached_y;}
            else bullet_forecast_pair(b,from,remaining,accel_x,accel_y,&start_x,&start_y);
            bullet_forecast_pair(b,t,remaining,accel_x,accel_y,&finish_x,&finish_y);
            cached_t=t;cached_x=finish_x;cached_y=finish_y;
            start_x-=g->player.x;start_y-=g->player.y;
            finish_x-=g->player.x;finish_y-=g->player.y;
            for(unsigned move=0U;move<9U;move++){
                int32_t cvx=candidate_x[move],cvy=candidate_y[move];
                int32_t ax=start_x-cvx*(int32_t)from,ay=start_y-cvy*(int32_t)from;
                int32_t bx=finish_x-cvx*(int32_t)t,by=finish_y-cvy*(int32_t)t;
                if(ai_bullet_segment_box(ax,ay,bx,by))risks[move]+=(4U-s)*12000U;
                if(abs32(bx)<36000 && abs32(by)<36000)risks[move]+=(uint32_t)(72000-abs32(bx)-abs32(by))/100U;
            }
        }
    }
    uint32_t best=UINT32_MAX;int32_t best_x=0,best_y=0;
    for(unsigned move=0U;move<9U;move++){
        int32_t cvx=candidate_x[move],cvy=candidate_y[move];
        int32_t end_x=g->player.x+cvx*14,end_y=g->player.y+cvy*14;
        uint32_t risk=risks[move];
        for(unsigned i=0U;i<GARDEN_MAX_ENEMIES;i++){
            const GardenEnemy *e=&g->enemies[i];if(!e->active)continue;
            int32_t ex=e->x+e->vx*9,ey=e->y+e->vy*9;
            int32_t rx=e->type==GARDEN_ENEMY_DARTER ? 39000:59000;
            int32_t ry=e->type==GARDEN_ENEMY_DARTER ? 39000:57000;
            if(segment_box(g->player.x,g->player.y,g->player.x+cvx*9,g->player.y+cvy*9,ex-rx,ey-ry,ex+rx,ey+ry))risk+=90000U;
        }
        if(g->boss.active && segment_box(g->player.x,g->player.y,end_x,end_y,
            g->boss.x-94000,g->boss.y-56000,g->boss.x+94000,g->boss.y+56000))risk+=90000U;
        if(g->laser.phase==GARDEN_LASER_FIRE || (g->laser.phase==GARDEN_LASER_WARN && g->laser.ticks<=14U)){
            for(unsigned i=0U;i<2U;i++)if(segment_box(g->player.x,g->player.y,end_x,end_y,
                g->laser.x[i]-24000,g->laser.y-15000,g->laser.x[i]+24000,495000))risk+=100000U;
        }
        if(risk<best){best=risk;best_x=cvx;best_y=cvy;}
    }
    *vx=best_x;*vy=best_y;
}


static void check_player_collisions(GardenGame *g,int32_t previous_x,int32_t previous_y,
                                    unsigned previous_laser_phase)
{
    unsigned int i;
    if (!g->player.active || g->player.grace || g->player.respawning) return;
    /* Explicit 9px square hit area, following the unused reference routine
     * ebullet.c:384-397, instead of upstream draw/check_pixel side effects.
     */
    unsigned hit=GARDEN_MAX_ENEMY_BULLETS;
    for (i = 0; i < garden_enemy_scan_limit; ++i) {
        GardenEnemyBullet *b = &g->enemy_bullets[i];
        unsigned armed=garden_enemy_bullet_is_armed(b);
        if(b->active && overlaps(b->x,b->y,g->player.x,g->player.y,9000,9000)){
            uint32_t *counter=armed ? &g->hazard.armed_contacts:&g->hazard.forming_contacts;
            *counter=combat_add(*counter,1U);
        }
        if (hit==GARDEN_MAX_ENEMY_BULLETS && armed &&
            ((b->grazed&2U) ? (unsigned)overlaps(b->x,b->y,g->player.x,g->player.y,9000,9000) :
             segment_box(b->x-b->vx-previous_x,b->y-b->vy-previous_y,
                b->x-g->player.x,b->y-g->player.y,-9000,-9000,9000,9000))) {
            hit=i;
        }
    }
    if(hit<GARDEN_MAX_ENEMY_BULLETS){
        GardenEnemyBullet *b=&g->enemy_bullets[hit];
        if(!overlaps(b->x,b->y,g->player.x,g->player.y,9000,9000))
            g->weapon_stats.swept_hits=combat_add(g->weapon_stats.swept_hits,1U);
        b->active=0U;damage_player(g);return;
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
    if(g->player.grace || g->player.respawning || !g->player.active)return;
    if(g->laser.phase==GARDEN_LASER_FIRE ||
       (previous_laser_phase==GARDEN_LASER_FIRE && g->laser.phase==GARDEN_LASER_FADE)){
        /* A newly fired beam starts at the current endpoint. Subsequent
         * FIRE movement is swept, including its last interval into FADE;
         * motion performed entirely during WARN/FADE is never retroactive.
         */
        int32_t from_x=previous_laser_phase==GARDEN_LASER_FIRE ? previous_x:g->player.x;
        int32_t from_y=previous_laser_phase==GARDEN_LASER_FIRE ? previous_y:g->player.y;
        for(unsigned i=0U;i<2U;i++)if(segment_box(from_x,from_y,g->player.x,g->player.y,
            g->laser.x[i]-GARDEN_LASER_HALF_WIDTH-9000,g->laser.y-9000,
            g->laser.x[i]+GARDEN_LASER_HALF_WIDTH+9000,GARDEN_HEIGHT*GARDEN_GRAIN+9000)){
            g->weapon_stats.laser_hits=combat_add(g->weapon_stats.laser_hits,1U);
            damage_player(g);return;
        }
    }
}

static void check_grazes(GardenGame *g)
{
    if (!g->player.active || g->player.grace || g->player.respawning ||
        (g->mode!=GARDEN_MODE_WAVES && g->mode!=GARDEN_MODE_BOSS)) return;
    for (unsigned i=0U;i<garden_enemy_scan_limit;i++) {
        GardenEnemyBullet *b=&g->enemy_bullets[i];
        if (!garden_enemy_bullet_is_armed(b) || (b->grazed&1U) ||
            !overlaps(b->x,b->y,g->player.x,g->player.y,28000,28000) ||
             overlaps(b->x,b->y,g->player.x,g->player.y,9000,9000)) continue;
        b->grazed|=1U;
        g->reward.graze_count=combat_add(g->reward.graze_count,1U);
        g->reward.graze_score_total=combat_add(g->reward.graze_score_total,
                                                 reward_score(g,GARDEN_GRAZE_SCORE));
    }
}

static void update_counts(GardenGame *g)
{
    unsigned int i;
    g->enemy_count = g->player_bullet_count = g->enemy_bullet_count = 0;
    g->hazard.armed=g->hazard.forming=0U;
    for (i = 0; i < GARDEN_MAX_ENEMIES; ++i)
        if (g->enemies[i].active) ++g->enemy_count;
    for (i = 0; i < GARDEN_MAX_PLAYER_BULLETS; ++i)
        if (g->player_bullets[i].active) ++g->player_bullet_count;
    for (i = 0; i < garden_enemy_scan_limit; ++i){
        GardenEnemyBullet *b=&g->enemy_bullets[i];
        if(garden_enemy_bullet_is_armed(b) && (b->x<=-10000 || b->x>=650000 || b->y<=-10000 || b->y>=490000))b->active=0U;
        if(b->active){++g->enemy_bullet_count;if(garden_enemy_bullet_is_armed(b))++g->hazard.armed;else ++g->hazard.forming;}
    }
    if (g->enemy_bullet_count > g->enemy_bullet_peak)
        g->enemy_bullet_peak = g->enemy_bullet_count;
}

static void init_defaults(GardenGame *g,uint32_t seed)
{
    garden_enemy_scan_limit=192U;
    g->initial_seed = seed ? seed : UINT32_C(0x47617264);
    g->rng = g->initial_seed;
    g->lives = 3; g->demo_mode = 1; g->mode = GARDEN_MODE_WAVES;
    g->player.active = 1; g->player.x = 320000; g->player.y = 410000;
    g->player.grace = 80;
    g->reward.bombs=2U;
}

void garden_init(GardenGame *g, uint32_t seed)
{
    /* Public init accepts arbitrary fresh/dirty storage and keeps the full
     * zeroed-state contract, including unscanned bullet slots and padding. */
    garden_clear_zero(g,sizeof(*g));
    init_defaults(g,seed);
}

static void restart_game(GardenGame *g)
{
    unsigned demo=g->demo_mode;
    uint32_t best=g->reward.high_score>g->score ? g->reward.high_score : g->score;
    uint32_t seed=seed_successor(g->initial_seed);
    /* Only a previously initialized game reaches this internal path. The
     * pressure driver calls public init before each extent change, so slots
     * above the prior extent remain zero. Capture it before defaults reset
     * the extent to192, and clear at least that normal-game prefix. */
    unsigned clear_slots=garden_enemy_scan_limit;
    if(clear_slots<192U)clear_slots=192U;
    if(clear_slots>GARDEN_MAX_ENEMY_BULLETS)clear_slots=GARDEN_MAX_ENEMY_BULLETS;
    garden_clear_zero(g,offsetof(GardenGame,enemy_bullets));
    garden_clear_zero(g->enemy_bullets,clear_slots*sizeof(g->enemy_bullets[0]));
    garden_clear_zero(&g->boss,sizeof(*g)-offsetof(GardenGame,boss));
    init_defaults(g,seed);
    g->demo_mode=(uint8_t)demo;
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
    if (buttons & GARDEN_INPUT_PAUSE){g->paused ^= 1u;if(g->paused){g->laser.phase=GARDEN_LASER_OFF;g->laser.ticks=0U;}}
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
        if(g->ordnance.blast_ticks){if(!--g->ordnance.blast_ticks)g->ordnance.active=0U;}
        if (g->mode==GARDEN_MODE_VICTORY && g->demo_mode && g->end_ticks >= GARDEN_END_TICKS) {
            restart_game(g); return;
        }
        update_counts(g); return;
    }
    g->reward.run_ticks=combat_add(g->reward.run_ticks,1U);
    int32_t previous_x=g->player.x,previous_y=g->player.y;
    unsigned previous_laser_phase=g->laser.phase;
    unsigned can_bomb=g->player.active && !g->player.respawning;
    run_player(g, buttons);
    if (g->mode == GARDEN_MODE_WAVES) run_waves(g);
    run_enemies(g);
    if (g->mode == GARDEN_MODE_BOSS){run_laser(g);run_boss(g);}
    run_enemy_bullets(g);
    if(can_bomb && ((buttons&GARDEN_INPUT_BOMB) || auto_throw(g)))fire_bomb(g);
    run_ordnance(g);
    run_player_bullets(g);
    check_player_collisions(g,previous_x,previous_y,previous_laser_phase);
    check_grazes(g);
    run_pickups(g);
    update_counts(g);
}
