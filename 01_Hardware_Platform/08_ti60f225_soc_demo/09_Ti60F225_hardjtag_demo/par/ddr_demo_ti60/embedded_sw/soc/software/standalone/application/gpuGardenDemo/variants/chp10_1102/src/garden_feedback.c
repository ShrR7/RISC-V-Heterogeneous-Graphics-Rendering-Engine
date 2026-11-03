/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure observer: no collision, RNG, HP, score, game-clock or MMIO writes.
 * Sparks are evidence-backed presentation, not a new damage-event system.
 */
#include "garden_feedback.h"
#include "garden_assets.h"
#include <string.h>

static GardenFeedbackTarget target_at(const GardenGame *game,unsigned i)
{
    if (i<GARDEN_MAX_ENEMIES) {
        const GardenEnemy *e=&game->enemies[i];
        return (GardenFeedbackTarget){e->x,e->y,e->hp,e->active,e->type,e->flash};
    }
    return (GardenFeedbackTarget){game->boss.x,game->boss.y,game->boss.hp,
                                  game->boss.active,0U,game->boss.flash};
}

static void save_snapshot(GardenFeedback *feedback,const GardenGame *game)
{
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES+1U;i++)
        feedback->targets[i]=target_at(game,i);
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) {
        const GardenPlayerBullet *b=&game->player_bullets[i];
        feedback->bullets[i]=(GardenFeedbackBullet){b->x,b->y,b->age,b->damage,b->active};
    }
    feedback->initialized=1U;
    feedback->last_tick=game->tick;
    feedback->last_score=game->score;
    feedback->last_mode=game->mode;
}

void garden_feedback_reset(GardenFeedback *feedback)
{
    if (feedback) memset(feedback,0,sizeof(*feedback));
}

static int16_t spark_pixel(int32_t coordinate,int maximum)
{
    int32_t pixel=coordinate/GARDEN_GRAIN;
    /* Keep a whole 13x13 spark inside the playfield. This affects only a
     * decorative edge sprite, never the player centre or collision geometry.
     */
    if (pixel<6) pixel=6;
    if (pixel>maximum-7) pixel=maximum-7;
    return (int16_t)pixel;
}

static void spawn_spark(GardenFeedback *feedback,int32_t x,int32_t y)
{
    if (feedback->sparks[GARDEN_FEEDBACK_SPARK_MAX-1U].active)
        feedback->stats.spark_candidates_dropped++;
    for (unsigned i=GARDEN_FEEDBACK_SPARK_MAX-1U;i>0U;i--)
        feedback->sparks[i]=feedback->sparks[i-1U];
    feedback->sparks[0]=(GardenHitSpark){spark_pixel(x,GARDEN_WIDTH),
                                       spark_pixel(y,GARDEN_HEIGHT),1U,0U};
    feedback->stats.sparks_spawned++;
}

static int within(int32_t x,int32_t y,const GardenFeedbackTarget *target,unsigned boss)
{
    int64_t dx=(int64_t)x-target->x;
    int64_t dy=(int64_t)y-target->y;
    int32_t rx,ry;
    if (boss) { rx=76000; ry=42000; }
    else if (target->type==GARDEN_ENEMY_DARTER) {
        rx=27000; ry=40000; dy-=4000;
    } else {
        rx=47000; ry=90000; dy+=30000;
    }
    return dx>=-rx && dx<=rx && dy>=-ry && dy<=ry;
}

static void fallback_spark(GardenFeedback *feedback,
                           const GardenFeedbackTarget *target,unsigned boss)
{
    unsigned id=boss ? GARDEN_ASSET_BOSS1 :
                target->type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1 : GARDEN_ASSET_STOPPER1;
    const garden_sprite_t *sprite=garden_asset(id);
    int32_t offset=sprite ? ((int32_t)sprite->height-sprite->anchor_y-1)*GARDEN_GRAIN : 0;
    /* No consumed bullet could be reliably identified (for example, a newly
     * born and consumed bullet between snapshots). Use the sprite's lower
     * edge, explicitly counted as fallback rather than claimed impact data.
     */
    int64_t lower=(int64_t)target->y+offset;
    if (lower>INT32_MAX) lower=INT32_MAX;
    if (lower<INT32_MIN) lower=INT32_MIN;
    spawn_spark(feedback,target->x,(int32_t)lower);
    feedback->stats.fallback_hits++;
}

void garden_feedback_observe(GardenFeedback *feedback,const GardenGame *game)
{
    if (!feedback || !game) return;
    if (!feedback->initialized) { save_snapshot(feedback,game); return; }
    if (game->tick<feedback->last_tick ||
        (game->mode==GARDEN_MODE_WAVES && feedback->last_mode!=GARDEN_MODE_WAVES) ||
        (game->mode!=feedback->last_mode && game->mode!=GARDEN_MODE_VICTORY)) {
        /* Victory retains the last killing hit; restart/other mode epochs do
         * not carry old impacts onto a new scene. Lifetime statistics stay.
         */
        memset(feedback->sparks,0,sizeof(feedback->sparks));
        save_snapshot(feedback,game);
        return;
    }
    if (game->tick==feedback->last_tick) return;
    uint32_t elapsed=game->tick-feedback->last_tick;
    for (unsigned i=0;i<GARDEN_FEEDBACK_SPARK_MAX;i++) {
        GardenHitSpark *spark=&feedback->sparks[i];
        if (!spark->active) continue;
        if (elapsed>=GARDEN_FEEDBACK_SPARK_TICKS-spark->age) spark->active=0U;
        else spark->age=(uint8_t)(spark->age+elapsed);
    }

    GardenFeedbackTarget current[GARDEN_MAX_ENEMIES+1U];
    int64_t remaining[GARDEN_MAX_ENEMIES+1U]={0};
    uint8_t affected[GARDEN_MAX_ENEMIES+1U]={0};
    uint8_t matched[GARDEN_MAX_ENEMIES+1U]={0};
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES+1U;i++) {
        const GardenFeedbackTarget *previous=&feedback->targets[i];
        current[i]=target_at(game,i);
        if (!previous->active) continue;
        int decreased=current[i].hp<previous->hp;
        int killed=!current[i].active && current[i].flash &&
                   game->score!=feedback->last_score && current[i].hp<=0;
        if (!decreased && !killed) continue;
        /* An offscreen despawn has unchanged HP and no score/flash evidence.
         * Require active HP decrease or the corroborated terminal transition.
         */
        if (!current[i].active && !killed) continue;
        affected[i]=1U;
        remaining[i]=decreased ? (int64_t)previous->hp-current[i].hp : 1;
        feedback->stats.hits++;
    }
    if (elapsed==1U) for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) {
        const GardenFeedbackBullet *previous=&feedback->bullets[i];
        const GardenPlayerBullet *bullet=&game->player_bullets[i];
        if (!previous->active || bullet->active || !bullet->damage ||
            bullet->damage!=previous->damage ||
            bullet->age!=(uint16_t)(previous->age+1U) || bullet->age>90U ||
            bullet->y<-30000 || bullet->y>510000 ||
            (int64_t)bullet->x!=(int64_t)previous->x+bullet->vx ||
            (int64_t)bullet->y!=(int64_t)previous->y+bullet->vy) continue;
        /* Core checks enemies in slot order, then Boss; use the same stable
         * order and consume observed HP deltas so one bullet is never reused.
         */
        for (unsigned j=0;j<GARDEN_MAX_ENEMIES+1U;j++) {
            if (!affected[j] || remaining[j]<=0 ||
                (j==GARDEN_MAX_ENEMIES && current[j].y<0) ||
                !within(bullet->x,bullet->y,&current[j],j==GARDEN_MAX_ENEMIES)) continue;
            spawn_spark(feedback,bullet->x,bullet->y);
            remaining[j]-=bullet->damage;
            if (!matched[j]) feedback->stats.bullet_anchored_hits++;
            matched[j]=1U;
            break;
        }
    }
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES+1U;i++) if (affected[i] && !matched[i])
        fallback_spark(feedback,&current[i],i==GARDEN_MAX_ENEMIES);
    save_snapshot(feedback,game);
}

static unsigned essential_count(const GardenGame *game)
{
    unsigned count=game->boss.active!=0U;
    count+=game->player.active && (!game->player.grace || (game->tick&3U)<2U);
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) count+=game->enemies[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) count+=game->player_bullets[i].active!=0U;
    for (unsigned i=0;i<garden_enemy_scan_limit;i++) count+=game->enemy_bullets[i].active!=0U;
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) count+=game->effects[i].active!=0U;
    return count;
}

unsigned garden_feedback_build(GardenFeedback *feedback,GardenFx *fx,
                               const GardenGame *game,GardenDraw *draws,
                               unsigned capacity,unsigned alpha_enabled,
                               unsigned focus)
{
    if (!feedback || !fx || !game) return 0U;
    if (!feedback->initialized) garden_feedback_observe(feedback,game);
    unsigned limit=capacity<GARDEN_DRAW_MAX ? capacity : GARDEN_DRAW_MAX;
    if (!draws) limit=0U;
    unsigned essential=essential_count(game);
    unsigned available=limit>essential ? limit-essential : 0U;
    unsigned marker_wanted=(focus || game->demo_mode) && game->player.active &&
                           !game->player.respawning;
    unsigned keep_marker=marker_wanted && available;
    available-=keep_marker;
    unsigned spark_wanted=0U;
    for (unsigned i=0;i<GARDEN_FEEDBACK_SPARK_MAX;i++)
        spark_wanted+=feedback->sparks[i].active!=0U;
    unsigned keep_sparks=spark_wanted<available ? spark_wanted : available;
    unsigned reserved=keep_marker+keep_sparks;
    /* Existing FX grants essential sprites before trails/rings. Subtracting
     * reservations makes marker/sparks win budget without losing an actor.
     * Its historical Alpha-panel reservation only caps an unreserved list at
     * 239; the hidden panel is no longer appended by our integration.
     */
    unsigned count=garden_fx_build(fx,game,draws,limit-reserved,alpha_enabled);
    unsigned kept=0U;
    for (unsigned i=0;i<GARDEN_FEEDBACK_SPARK_MAX && kept<keep_sparks;i++) {
        const GardenHitSpark *spark=&feedback->sparks[i];
        if (!spark->active) continue;
        if (count>=limit) break;
        unsigned alpha=spark->age<2U ? 32U : spark->age<4U ? 20U : 10U;
        draws[count++]=(GardenDraw){spark->x,spark->y,GARDEN_ASSET_HIT_SPARK,
                                   (uint8_t)(alpha_enabled ? alpha : 32U)};
        kept++;
    }
    unsigned marker_drawn=0U;
    if (keep_marker && count<limit) {
        /* Marker centre is exactly core x/y, unaffected by grace blink. Its
         * two forms mean vulnerable / protected; it never changes hitbox.
         */
        draws[count++]=(GardenDraw){(int16_t)(game->player.x/GARDEN_GRAIN),
            (int16_t)(game->player.y/GARDEN_GRAIN),
            (uint16_t)(game->player.grace ? GARDEN_ASSET_FOCUS_GRACE : GARDEN_ASSET_FOCUS_VULNERABLE),32U};
        marker_drawn=1U;
    }
    GardenFeedbackStats *stats=&feedback->stats;
    stats->sparks=kept;
    stats->marker=marker_drawn;
    stats->essential_dropped=fx->stats.essential_dropped;
    stats->decorative_dropped=fx->stats.decorative_dropped+
                             spark_wanted-kept+marker_wanted-marker_drawn;
    stats->builds++;
    if (kept) stats->spark_frames++;
    if (marker_drawn) stats->marker_frames++;
    if (count>stats->peak_draws) stats->peak_draws=count;
    stats->essential_drop_total+=stats->essential_dropped;
    stats->decorative_drop_total+=stats->decorative_dropped;
    return count;
}

const GardenFeedbackStats *garden_feedback_stats(const GardenFeedback *feedback)
{ return feedback ? &feedback->stats : 0; }
