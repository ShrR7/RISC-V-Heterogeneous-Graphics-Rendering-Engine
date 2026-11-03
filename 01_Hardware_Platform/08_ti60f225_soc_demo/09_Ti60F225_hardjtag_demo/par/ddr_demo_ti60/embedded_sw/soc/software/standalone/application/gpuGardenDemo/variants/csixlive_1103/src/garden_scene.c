/* SPDX-License-Identifier: GPL-3.0-or-later
 * Fixed-size presentation observer of the real40Hz Boss rhythm schedule.
 */
#include "garden_scene.h"
#include "garden_assets.h"
#include "garden_ordnance_view.h"
#include <string.h>

static int16_t pixel(int32_t coordinate);

static unsigned smooth_q8(unsigned u)
{
    if (u>=256U) return 256U;
    return (u*u*(768U-2U*u)+32768U)/65536U;
}

static unsigned shell_open(const GardenScene *scene,const GardenGame *game,unsigned fraction)
{
    const GardenBoss *b=&game->boss;
    if (b->rhythm_state==GARDEN_BOSS_CHARGE && b->charge_ticks) {
        unsigned remaining=b->rhythm_ticks<b->charge_ticks ? b->rhythm_ticks : b->charge_ticks;
        unsigned elapsed=(b->charge_ticks-remaining)*256U+fraction;
        return smooth_q8(elapsed/b->charge_ticks);
    }
    if (b->rhythm_state==GARDEN_BOSS_BURST) return 256U;
    if (b->rhythm_state==GARDEN_BOSS_REST) {
        unsigned remaining=b->rhythm_ticks<b->rest_ticks ? b->rhythm_ticks : b->rest_ticks;
        unsigned elapsed=(b->rest_ticks-remaining)*256U+fraction;
        return 256U-smooth_q8(elapsed/GARDEN_SCENE_CLOSE_TICKS);
    }
    if (b->rhythm_state==GARDEN_BOSS_WAIT && scene->wait_from_q8) {
        uint32_t age=game->tick-scene->wait_started_tick;
        if (age>=GARDEN_SCENE_CLOSE_TICKS) return 0U;
        unsigned close=smooth_q8((age*256U+fraction)/GARDEN_SCENE_CLOSE_TICKS);
        return (scene->wait_from_q8*(256U-close)+128U)/256U;
    }
    return 0U;
}

static void history_snap(GardenScene *scene,const GardenGame *game)
{
    for (unsigned i=0;i<4U;i++) {
        scene->boss_x_history[i]=pixel(game->boss.x);
        scene->boss_y_history[i]=pixel(game->boss.y);
    }
    scene->history_valid=game->boss.active!=0U;
}

/* Observe the true gameplay charge. Movement/rest/capacity WAIT has no fake
 * forecast. Subsequent train pulses use the observed release flash, and the
 * first pulse always follows the complete20/16/12tick charge.
 */
static unsigned charge_countdown(const GardenGame *game)
{
    return garden_boss_fire_countdown(game);
}

static unsigned low_hp(const GardenGame *game)
{
    return game->boss.active && game->boss.max_hp>0 && game->boss.hp>0 &&
           (int64_t)game->boss.hp*2<=game->boss.max_hp;
}

static void snapshot(GardenScene *scene,const GardenGame *game)
{
    scene->initialized=1U;
    scene->last_tick=game->tick;
    scene->last_mode=game->mode;
    scene->last_boss_active=game->boss.active;
    scene->last_attack=game->boss.attack;
    scene->low_hp=(uint8_t)low_hp(game);
    for (unsigned i=0;i<3U;i++) scene->last_shots[i]=game->boss_attack_shots[i];
    scene->charge_countdown=(uint16_t)charge_countdown(game);
    scene->last_rhythm_state=game->boss.rhythm_state;
    scene->last_open_q8=(uint16_t)shell_open(scene,game,0U);
}

void garden_scene_reset(GardenScene *scene)
{
    if (!scene) return;
    memset(scene,0,sizeof(*scene));
    scene->enabled=1U;
}

void garden_scene_set_enabled(GardenScene *scene,unsigned enabled)
{
    if (!scene) return;
    enabled=enabled!=0U;
    if (scene->enabled!=enabled) {
        scene->enabled=enabled;
        scene->initialized=0U;
        scene->burst_ticks=0U;
        scene->charge_countdown=0U;
        scene->history_valid=0U;
        scene->render_fraction=0U;
        scene->last_open_q8=scene->wait_from_q8=0U;
    }
}

void garden_scene_observe(GardenScene *scene,const GardenGame *game)
{
    if (!scene || !game) return;
    scene->paused=game->paused!=0U;
    unsigned rollback=0U;
    for (unsigned i=0;i<3U;i++) rollback|=game->boss_attack_shots[i]<scene->last_shots[i];
    if (!scene->initialized || game->tick<scene->last_tick ||
        game->mode!=scene->last_mode || rollback) {
        scene->burst_ticks=0U;
        scene->render_fraction=0U;
        scene->wait_from_q8=0U;
        scene->wait_started_tick=game->tick;
        history_snap(scene,game);
        snapshot(scene,game);
        return;
    }
    if (game->tick==scene->last_tick) return;
    uint32_t elapsed=game->tick-scene->last_tick;
    if (elapsed!=1U || !scene->history_valid || !game->boss.active) history_snap(scene,game);
    else {
        for (unsigned i=3U;i>0U;i--) {
            scene->boss_x_history[i]=scene->boss_x_history[i-1U];
            scene->boss_y_history[i]=scene->boss_y_history[i-1U];
        }
        scene->boss_x_history[0]=pixel(game->boss.x);
        scene->boss_y_history[0]=pixel(game->boss.y);
    }
    if (game->boss.rhythm_state==GARDEN_BOSS_WAIT && scene->last_rhythm_state!=GARDEN_BOSS_WAIT) {
        scene->wait_from_q8=scene->last_open_q8;
        scene->wait_started_tick=game->tick;
    }
    scene->burst_ticks=elapsed>=scene->burst_ticks ? 0U :
                       (uint8_t)(scene->burst_ticks-elapsed);
    unsigned active=game->mode==GARDEN_MODE_BOSS && game->boss.active;
    uint32_t shots=0U;
    if (active && scene->last_boss_active)
        for (unsigned i=0;i<3U;i++) shots+=game->boss_attack_shots[i]-scene->last_shots[i];
    if (scene->enabled && active) {
        scene->stats.attack_shots_observed+=shots;
        /* A skipped-observation gap cannot date a shot precisely. Count the
         * real counter increment, but do not invent a fresh burst afterward.
         */
        if (shots && elapsed==1U) scene->burst_ticks=GARDEN_SCENE_BURST_TICKS;
        if (scene->last_boss_active && game->boss.attack!=scene->last_attack)
            scene->stats.attack_changes_observed++;
        if (scene->last_boss_active && low_hp(game)!=scene->low_hp)
            scene->stats.half_hp_transitions++;
        if (!scene->charge_countdown && charge_countdown(game)) scene->stats.charge_windows++;
    }
    if (!active || !scene->enabled) scene->burst_ticks=0U;
    snapshot(scene,game);
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

static int16_t pixel(int32_t coordinate)
{
    int32_t p=coordinate/GARDEN_GRAIN;
    if (p>32000) p=32000;
    if (p<-32000) p=-32000;
    return (int16_t)p;
}

static uint8_t opacity(unsigned alpha_enabled,unsigned alpha)
{ return (uint8_t)(alpha_enabled ? alpha : 32U); }

void garden_scene_set_render_fraction(GardenScene *scene,unsigned q8)
{
    if (!scene || scene->paused) return;
    scene->render_fraction=(uint16_t)(q8>255U ? 255U : q8);
}

static int lag(const int16_t *history,unsigned delay,int current,unsigned fraction)
{
    int followed=history[delay]+((int)history[delay-1U]-history[delay])*(int)fraction/256;
    int d=followed-current;
    return d<-6 ? -6 : d>6 ? 6 : d;
}

static void union_sprite(const GardenDraw *draw,int *left,int *top,int *right,int *bottom)
{
    const garden_sprite_t *sprite=garden_asset(draw->asset);
    if (!sprite) return;
    int x=draw->x-sprite->anchor_x,y=draw->y-sprite->anchor_y;
    if (x<*left) *left=x;
    if (y<*top) *top=y;
    if (x+(int)sprite->width>*right) *right=x+sprite->width;
    if (y+(int)sprite->height>*bottom) *bottom=y+sprite->height;
}

unsigned garden_scene_boss_pose(const GardenScene *scene,const GardenGame *game,GardenSceneBossPose *pose)
{
    if (!pose) return 0U;
    memset(pose,0,sizeof(*pose));
    if (!scene || !game || !scene->enabled || game->mode!=GARDEN_MODE_BOSS || !game->boss.active) return 0U;
    unsigned fraction=scene->render_fraction;
    unsigned open=shell_open(scene,game,fraction);
    unsigned phase=(game->tick%64U)*256U+fraction;
    unsigned triangle=phase<8192U ? phase : 16384U-phase;
    unsigned breathe=smooth_q8(triangle/32U);
    int lift=-3+(int)((6U*breathe+128U)/256U);
    unsigned recoil=scene->burst_ticks ? scene->burst_ticks*256U-fraction : 0U;
    int kick=(int)((2U*recoil+512U)/1024U);
    int x=pixel(game->boss.x),y=pixel(game->boss.y);
    int spread=(int)((4U*open+128U)/256U);
    pose->open_q8=open;
    for (unsigned i=0;i<2U;i++) {
        int dx=scene->history_valid ? lag(scene->boss_x_history,i+2U,x,fraction) : 0;
        int dy=scene->history_valid ? lag(scene->boss_y_history,i+2U,y,fraction) : 0;
        int mount_x=i ? GARDEN_BOSS_MUZZLE_RIGHT_X_PX:GARDEN_BOSS_MUZZLE_LEFT_X_PX;
        /* Each cropped new-cruiser pod is anchored at its own last opaque
         * barrel pixel. Closed neutral offsets reconstruct the source ship;
         * only harmless idle/charge poses add bounded secondary movement. */
        pose->wings[i]=(GardenDraw){(int16_t)(x+mount_x+(i ? spread:-spread)+dx),
            (int16_t)(y+GARDEN_BOSS_MUZZLE_Y_PX+lift+kick+dy),
            (uint16_t)(i ? GARDEN_ASSET_BOSS_WING_R : GARDEN_ASSET_BOSS_WING_L),32U};
        /* A warning fixes the actual attack rails. The complete wing pod is
         * attached to that same locked muzzle, including the last harmless
         * fade. Cosmetic lag never shifts a displayed beam away from its
         * authoritative collision coordinates. */
        if(garden_ordnance_laser_count(game)) {
            pose->wings[i].x=(int16_t)(game->laser.x[i]/GARDEN_GRAIN);
            pose->wings[i].y=(int16_t)(game->laser.y/GARDEN_GRAIN);
        }
    }
    unsigned core_alpha=20U+(10U*breathe)/256U+(2U*open)/256U+(2U*recoil)/1024U;
    if (core_alpha>32U) core_alpha=32U;
    pose->core=(GardenDraw){(int16_t)x,(int16_t)(y+GARDEN_BOSS_CORE_Y_PX),GARDEN_ASSET_BOSS_CORE,(uint8_t)core_alpha};
    unsigned cue_alpha=0U;
    if (scene->charge_countdown) cue_alpha=4U+(24U*open)/256U;
    else if (game->boss.rhythm_state==GARDEN_BOSS_BURST) cue_alpha=28U;
    else if (recoil) cue_alpha=(28U*recoil)/1024U;
    pose->cue_visible=cue_alpha!=0U;
    pose->cue=(GardenDraw){(int16_t)x,(int16_t)(y+GARDEN_BOSS_NOSE_Y_PX),GARDEN_ASSET_BOSS_CHARGE,(uint8_t)cue_alpha};
    int left=x,top=y,right=x,bottom=y;
    GardenDraw body={(int16_t)x,(int16_t)y,GARDEN_ASSET_BOSS1,32U};
    union_sprite(&body,&left,&top,&right,&bottom);
    for (unsigned i=0;i<2U;i++) union_sprite(&pose->wings[i],&left,&top,&right,&bottom);
    union_sprite(&pose->core,&left,&top,&right,&bottom);
    if (pose->cue_visible) union_sprite(&pose->cue,&left,&top,&right,&bottom);
    pose->left=(int16_t)left;pose->top=(int16_t)top;
    pose->right=(int16_t)right;pose->bottom=(int16_t)bottom;
    return 1U;
}

static GardenDraw backdrop(unsigned index,uint32_t tick,unsigned fraction,unsigned alpha_enabled)
{
    unsigned far=index<4U;
    unsigned i=index&3U;
    /* Independent exact1/2px/tick and3/2px/tick scrolls:20and60px/s. Finite modulus
     * before multiplication keeps large ticks defined; no RNG is consumed.
     * The centre travels through [-24,503] so entry/exit is outside the field.
     */
    unsigned scroll=far ? ((tick%1056U)*256U+fraction)/512U :
                         (((tick%352U)*256U+fraction)*3U)/512U;
    int y=(int)((scroll+i*132U)%528U)-24;
    int x=far ? 78+(int)i*160 : 42+(int)i*174;
    return (GardenDraw){(int16_t)x,(int16_t)y,
        (uint16_t)(far ? GARDEN_ASSET_GARDEN_FAR : GARDEN_ASSET_GARDEN_STREAK),
        opacity(alpha_enabled,far ? 24U : 26U)};
}

unsigned garden_scene_build(GardenScene *scene,GardenFeedback *feedback,GardenFx *fx,
                           const GardenGame *game,GardenDraw *draws,
                           unsigned capacity,unsigned alpha_enabled,unsigned focus)
{
    if (!scene || !feedback || !fx || !game) return 0U;
    if (!scene->initialized) garden_scene_observe(scene,game);
    if (!feedback->initialized) garden_feedback_observe(feedback,game);
    unsigned limit=capacity<GARDEN_DRAW_MAX ? capacity : GARDEN_DRAW_MAX;
    if (!draws) limit=0U;
    unsigned active=scene->enabled && game->mode==GARDEN_MODE_BOSS && game->boss.active;
    GardenSceneBossPose pose;
    unsigned have_pose=garden_scene_boss_pose(scene,game,&pose);
    unsigned cue=have_pose && pose.cue_visible;
    unsigned essential=essential_count(game);
    unsigned marker=(focus || game->demo_mode) && game->player.active && !game->player.respawning;
    unsigned sparks=0U;
    for (unsigned i=0;i<GARDEN_FEEDBACK_SPARK_MAX;i++) sparks+=feedback->sparks[i].active!=0U;
    unsigned laser_wanted=garden_ordnance_laser_count(game);
    unsigned high=essential+marker+sparks;
    unsigned available=limit>high ? limit-high : 0U;
    unsigned lasers=laser_wanted && available>=laser_wanted ? laser_wanted:0U;
    available-=lasers;
    /* Admit complete paired wings, then the reactor and cue. If only a single
     * slot is free the reactor wins over a visibly one-sided wing assembly.
     */
    unsigned wings=active && available>=2U ? 2U : 0U;
    available-=wings;
    unsigned core=active && available;
    available-=core;
    unsigned charge=cue && available;
    available-=charge;
    unsigned muzzles=lasers && available>=2U ? 2U:0U;
    unsigned reserved=wings+core+charge+lasers+muzzles;
    unsigned count=garden_feedback_build(feedback,fx,game,draws,limit-reserved,alpha_enabled,focus);
    /* Feedback's bounded build has admitted essentials/marker/sparks before
     * old decor. Reservation cannot remove those high-priority entries.
     */
    unsigned old_decor=fx->stats.ghosts+fx->stats.rings;
    unsigned nbody=0U;
    if (active) for (unsigned i=old_decor;i<count;i++)
        if (draws[i].asset==GARDEN_ASSET_BOSS1 || draws[i].asset==GARDEN_ASSET_FLASH_BOSS1) { nbody=i+1U; break; }
    /* A malformed caller snapshot can claim an active Boss yet omit its body
     * through capacity truncation. Never paint a floating reactor in that case.
     */
    if (!nbody) core=0U;
    unsigned actual_reserved=wings+core+charge+lasers+muzzles;
    unsigned background_wanted=scene->enabled ? GARDEN_SCENE_BACKDROP_MAX : 0U;
    unsigned backgrounds=limit>count+actual_reserved ? limit-count-actual_reserved : 0U;
    if (backgrounds>background_wanted) backgrounds=background_wanted;
    if (core) {
        memmove(draws+nbody+1U,draws+nbody,(count-nbody)*sizeof(*draws));
        draws[nbody]=pose.core;draws[nbody].alpha=opacity(alpha_enabled,pose.core.alpha);
        count++;
    }
    /* Prefix background, keep old decorative prefix, then append mechanical
     * shell behind the original actors. Projectiles and feedback stay on top.
     */
    unsigned shell=wings+charge+lasers+muzzles;
    if (shell) {
        memmove(draws+old_decor+shell,draws+old_decor,(count-old_decor)*sizeof(*draws));
        unsigned index=old_decor;
        if(lasers) index+=garden_ordnance_laser_build(game,draws+index,lasers,alpha_enabled);
        if (wings) {
            draws[index++]=pose.wings[0];draws[index++]=pose.wings[1];
        }
        if (charge) {
            /* Existing energy art stays attached to the new cruiser's nose,
             * behind the hull and distinct from the two real laser muzzles. */
            draws[index]=pose.cue;draws[index].alpha=opacity(alpha_enabled,pose.cue.alpha);index++;
        }
        if(muzzles) (void)garden_ordnance_muzzle_build(game,draws+index,muzzles,alpha_enabled);
        count+=shell;
    }
    if (backgrounds) {
        memmove(draws+backgrounds,draws,count*sizeof(*draws));
        /* Alternating admission preserves both layers when only two fit. */
        for (unsigned i=0;i<backgrounds;i++)
            draws[i]=backdrop((i/2U)+(i&1U)*4U,scene->last_tick,scene->render_fraction,alpha_enabled);
        count+=backgrounds;
    }
    GardenSceneStats *stats=&scene->stats;
    stats->wings=wings; stats->core=core; stats->charge=charge; stats->backgrounds=backgrounds;
    stats->charge_countdown=active ? scene->charge_countdown : 0U;
    stats->burst_active=active && scene->burst_ticks;
    stats->essential_dropped=feedback->stats.essential_dropped+laser_wanted-lasers;
    stats->decorative_dropped=feedback->stats.decorative_dropped+
        (active ? 3U+cue : 0U)+(laser_wanted ? 2U:0U)-(actual_reserved-lasers)+background_wanted-backgrounds;
    stats->builds++;
    if (wings) stats->wing_frames++;
    if (core) stats->core_frames++;
    if (charge && scene->charge_countdown) stats->charge_frames++;
    if (charge && scene->burst_ticks) stats->burst_frames++;
    if (backgrounds) stats->background_frames++;
    if (count>stats->peak_draws) stats->peak_draws=count;
    stats->essential_drop_total+=stats->essential_dropped;
    stats->decorative_drop_total+=stats->decorative_dropped;
    return count;
}

const GardenSceneStats *garden_scene_stats(const GardenScene *scene)
{ return scene ? &scene->stats : 0; }
