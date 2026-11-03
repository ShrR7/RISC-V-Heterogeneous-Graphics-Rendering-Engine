/* SPDX-License-Identifier: GPL-3.0-or-later
 * This observer owns presentation epochs only. garden_core remains the sole
 * owner of movement, attacks, RNG, collision, score, and end_ticks.
 */
#include "garden_stage.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "garden_assets.h"
#include <string.h>

enum {
    STAGE_X=504, STAGE_Y=634, STAGE_W=444, STAGE_H=44,
    STAGE_WARNING_TICKS=100, STAGE_FADE_TICKS=64
};

void garden_stage_ui_reset(GardenStage *stage)
{
    if (!stage) return;
    stage->cache[0].valid=stage->cache[1].valid=0U;
}

void garden_stage_reset(GardenStage *stage)
{
    if (!stage) return;
    memset(stage,0,sizeof(*stage));
    stage->enabled=1U;
}

void garden_stage_set_enabled(GardenStage *stage,unsigned enabled)
{
    if (!stage) return;
    /* Retain epochs while disabled, so enabling never replays an entrance. */
    stage->enabled=enabled!=0U;
}

void garden_stage_observe(GardenStage *stage,const GardenGame *game)
{
    unsigned restarted;
    if (!stage || !game) return;
    if (game->boss.active) {
        stage->death_x=(int16_t)(game->boss.x/GARDEN_GRAIN);
        stage->death_y=(int16_t)(game->boss.y/GARDEN_GRAIN);
    }
    if (!stage->initialized) {
        stage->initialized=1U;
        stage->last_tick=game->tick;
        stage->last_mode=game->mode;
        stage->victory_tick=game->tick;
        stage->last_score=game->score;
        stage->victory_base_score=0U;
        return;
    }
    /* Core restart resets tick to zero. A mode return to WAVES also guards
     * a synthetic same-tick restart without treating attack changes as one.
     */
    restarted=game->tick<stage->last_tick ||
              (game->mode==GARDEN_MODE_WAVES && stage->last_mode!=GARDEN_MODE_WAVES);
    if (restarted) {
        stage->stats.restarts++;
        stage->boss_entry_valid=0U;
        stage->boss_entry_tick=stage->victory_tick=game->tick;
        stage->last_tick=game->tick;
        stage->last_mode=game->mode;
        stage->last_score=game->score;
        stage->victory_base_score=game->score;
        return;
    }
    if (game->tick==stage->last_tick) return;
    if (stage->last_mode==GARDEN_MODE_WAVES && game->mode==GARDEN_MODE_BOSS) {
        stage->boss_entry_tick=game->tick;
        stage->boss_entry_valid=1U;
        stage->stats.boss_entries++;
    }
    if (stage->last_mode!=GARDEN_MODE_VICTORY && game->mode==GARDEN_MODE_VICTORY) {
        stage->victory_tick=game->tick;
        stage->victory_base_score=stage->last_score;
        stage->stats.victories++;
    }
    /* A real late pickup can still credit the cleared field. Refresh static
     * BEST/PICKUPS once per physical buffer on that event, never on each
     * display-only score increment. */
    if (game->mode==GARDEN_MODE_VICTORY && game->score!=stage->last_score &&
        game->tick-stage->victory_tick>=GARDEN_STAGE_DEATH_TICKS)
        garden_stage_ui_reset(stage);
    if (game->mode!=GARDEN_MODE_BOSS) stage->boss_entry_valid=0U;
    if (stage->boss_entry_valid && game->boss.active &&
        game->tick-stage->boss_entry_tick<STAGE_FADE_TICKS)
        stage->stats.boss_fade_samples++;
    stage->last_tick=game->tick;
    stage->last_mode=game->mode;
    stage->last_score=game->score;
}

unsigned garden_stage_boss_alpha(const GardenStage *stage,
                               const GardenGame *game,unsigned alpha_enabled)
{
    uint32_t age;
    if (!stage || !game || !stage->enabled || !stage->initialized ||
        !alpha_enabled || !stage->boss_entry_valid ||
        game->mode!=GARDEN_MODE_BOSS || !game->boss.active)
        return 32U;
    /* last_tick, not wall time or render frames, makes pausing freeze a fade. */
    age=stage->last_tick-stage->boss_entry_tick;
    if (age>=STAGE_FADE_TICKS) return 32U;
    return 8U+(unsigned)(24U*age/(STAGE_FADE_TICKS-1U));
}

static GardenStageCache presentation(const GardenStage *stage,const GardenGame *game)
{
    GardenStageCache result={0};
    uint32_t age;
    result.valid=1U;
    if (!stage->enabled) return result;
    if (game->mode==GARDEN_MODE_VICTORY) {
        age=stage->last_tick-stage->victory_tick;
        result.kind=age>=GARDEN_STAGE_DEATH_TICKS ? GARDEN_STAGE_RESULTS : GARDEN_STAGE_FOOTER;
        result.score=garden_stage_visible_score(stage,game);
        result.demo=game->demo_mode!=0U;
        result.progress=(uint8_t)(age>=GARDEN_STAGE_PROMPT_TICKS);
    } else if (game->mode==GARDEN_MODE_BOSS && stage->boss_entry_valid) {
        age=stage->last_tick-stage->boss_entry_tick;
        if (age<STAGE_WARNING_TICKS) {
            result.kind=GARDEN_STAGE_WARNING;
            result.pulse=(uint8_t)((age/8U)&1U);
            result.progress=(uint8_t)(age/10U);
            result.demo=game->demo_mode!=0U;
        }
    }
    return result;
}

static char *decimal(char *out,uint32_t value)
{
    char reversed[10];
    unsigned count=0U;
    do { reversed[count++]=(char)('0'+value%10U); value/=10U; } while (value);
    while (count) *out++=reversed[--count];
    *out=0;
    return out;
}

unsigned garden_stage_modal_active(const GardenStage *stage,const GardenGame *game)
{
    return stage && game && stage->enabled && stage->initialized &&
           game->mode==GARDEN_MODE_VICTORY &&
           stage->last_tick-stage->victory_tick>=GARDEN_STAGE_DEATH_TICKS;
}

uint32_t garden_stage_visible_score(const GardenStage *stage,const GardenGame *game)
{
    unsigned age,roll;
    uint32_t base,delta;
    if (!game) return 0U;
    if (!stage || !stage->enabled || !stage->initialized || game->mode!=GARDEN_MODE_VICTORY) return game->score;
    age=stage->last_tick-stage->victory_tick;
    base=stage->victory_base_score<game->score ? stage->victory_base_score:game->score;
    if (age<GARDEN_STAGE_DEATH_TICKS) return base;
    roll=age-GARDEN_STAGE_DEATH_TICKS;
    if (roll>=GARDEN_STAGE_SCORE_TICKS) return game->score;
    delta=game->score-base;
    /* Split first to avoid overflow even for a UINT32_MAX final score. */
    return base+(delta/GARDEN_STAGE_SCORE_TICKS)*roll+
                (delta%GARDEN_STAGE_SCORE_TICKS)*roll/GARDEN_STAGE_SCORE_TICKS;
}

static void clear_field(void)
{
    garden_render_box(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT,0U);
    garden_ui_stars(GARDEN_X0,GARDEN_Y0,GARDEN_WIDTH,GARDEN_HEIGHT);
}

static int renderer_changed(uint32_t before)
{
    const GardenRenderStats *r=garden_render_stats();
    return before!=r->errors+r->bad_targets+r->atlas_bounds;
}

int garden_stage_prepare_frame(GardenStage *stage,const GardenGame *game,unsigned buffer)
{
    const GardenRenderStats *r;
    uint32_t before;
    if (!stage || !game) return -1;
    if (buffer>=2U) return -1;
    if (!(stage->modal_buffers&(1U<<buffer)) || garden_stage_modal_active(stage,game)) return 0;
    r=garden_render_stats();before=r->errors+r->bad_targets+r->atlas_bounds;
    clear_field();
    if (renderer_changed(before)) { garden_stage_ui_reset(stage);return -2; }
    stage->modal_buffers&=(uint8_t)~(1U<<buffer);
    stage->cache[buffer].valid=0U;
    stage->stats.modal_clears++;
    return 1;
}

static void value_text(int x,int y,const char *label,uint32_t value)
{
    char text[48];
    unsigned len=(unsigned)strlen(label);
    memcpy(text,label,len);
    decimal(text+len,value);
    garden_render_text_style(x,y,text,GARDEN_TEXT_SMALL_WHITE);
}

static void modal_score(uint32_t score)
{
    char text[11];
    decimal(text,score);
    garden_render_box(496,264,288,24,GARDEN_UI_PANEL);
    garden_render_text_style(496,266,text,GARDEN_TEXT_LARGE_WHITE);
}

static void modal_prompt(const GardenStageCache *view)
{
    garden_render_box(432,506,416,24,GARDEN_UI_PANEL);
    if (view->progress) garden_render_text_style(432,512,
        view->demo ? "R TO PLAY // AUTO SORTIE SOON" : "PRESS R FOR ANOTHER SORTIE",
        GARDEN_TEXT_SMALL_CYAN);
}

static void modal_draw(const GardenStageCache *view,const GardenGame *game,unsigned full)
{
    if (full) {
        clear_field();
        /* Draw only the visible 4px frame; the panel below owns its interior. */
        garden_render_box(384,176,512,4,GARDEN_UI_EDGE);
        garden_render_box(384,556,512,4,GARDEN_UI_EDGE);
        garden_render_box(384,180,4,376,GARDEN_UI_EDGE);
        garden_render_box(892,180,4,376,GARDEN_UI_EDGE);
        garden_render_box(388,180,504,376,GARDEN_UI_PANEL);
        garden_render_box(432,204,416,3,GARDEN_UI_GOLD);
        garden_render_text_style(552,220,"STAGE CLEAR",GARDEN_TEXT_LARGE_WHITE);
        garden_render_text_style(432,272,"SCORE",GARDEN_TEXT_SMALL_CYAN);
        value_text(432,312,"BEST ",game->reward.high_score);
        value_text(432,340,"TIME SECONDS ",game->reward.run_ticks/GARDEN_LOGIC_HZ);
        value_text(432,368,"HITS TAKEN ",game->reward.hits_taken);
        value_text(432,396,"GRAZE ",game->reward.graze_count);
        value_text(640,396,"PICKUPS ",game->reward.items_collected);
        value_text(432,424,"BOMBS USED ",game->reward.bombs_used);
        garden_render_text_style(432,464,
            game->reward.hits_taken==0U ? "RANK S // FLAWLESS" :
            game->reward.hits_taken<=2U ? "RANK A // STAGE COMPLETE" :
            game->reward.hits_taken<=4U ? "RANK B // STAGE COMPLETE" : "RANK C // STAGE COMPLETE",
            GARDEN_TEXT_SMALL_CYAN);
    }
    modal_score(view->score);
    modal_prompt(view);
}

static unsigned death_add(GardenDraw *out,unsigned n,unsigned capacity,int x,int y,
                          unsigned asset,unsigned alpha,unsigned alpha_enabled)
{
    if (n<capacity) out[n++]=(GardenDraw){(int16_t)x,(int16_t)y,(uint16_t)asset,
                                        (uint8_t)(alpha_enabled ? alpha:32U)};
    return n;
}

unsigned garden_stage_append_effects(GardenStage *stage,const GardenGame *game,
                                    GardenDraw *out,unsigned n,unsigned capacity,
                                    unsigned alpha_enabled)
{
    static const int8_t vx[8]={0,3,4,3,0,-3,-4,-3};
    static const int8_t vy[8]={-4,-3,0,3,4,3,0,-3};
    unsigned start,age;
    int x,y;
    if (!stage || !game || (!out && capacity)) return n;
    if (capacity>GARDEN_DRAW_MAX) capacity=GARDEN_DRAW_MAX;
    if (n>capacity) n=capacity;
    start=n;
    if (!stage->enabled || !stage->initialized || game->mode!=GARDEN_MODE_VICTORY) return n;
    age=stage->last_tick-stage->victory_tick;
    if (age>=GARDEN_STAGE_DEATH_TICKS) return n;
    x=stage->death_x;y=stage->death_y;
    if (age<32U) {
        unsigned alpha=32U-age*24U/32U;
        n=death_add(out,n,capacity,x,y,GARDEN_ASSET_BOSS1,alpha,alpha_enabled);
        n=death_add(out,n,capacity,x+GARDEN_BOSS_MUZZLE_LEFT_X_PX-(int)age*2,
                    y+GARDEN_BOSS_MUZZLE_Y_PX+(int)age/2,
                    GARDEN_ASSET_BOSS_WING_L,alpha,alpha_enabled);
        n=death_add(out,n,capacity,x+GARDEN_BOSS_MUZZLE_RIGHT_X_PX+(int)age*2,
                    y+GARDEN_BOSS_MUZZLE_Y_PX+(int)age/2,
                    GARDEN_ASSET_BOSS_WING_R,alpha,alpha_enabled);
    }
    /* Eight small radial fragments remain visible for two seconds, never an
     * opaque second full Boss image. The intact core survives until its own
     * delayed burst at1.5s; this is staged sprite debris, not petal geometry. */
    if (age>=12U && age<92U) {
        unsigned t=age-12U,alpha=32U-t*24U/80U;
        for (unsigned i=0U;i<(age<32U ? 6U:8U);i++) {
            int dx=(int)vx[i]*(int)t/2,dy=(int)vy[i]*(int)t/2+(int)(t*t)/100;
            n=death_add(out,n,capacity,x+dx,y+dy,GARDEN_ASSET_HIT_SPARK,alpha,alpha_enabled);
        }
    }
    if (age<60U) n=death_add(out,n,capacity,x,y+GARDEN_BOSS_CORE_Y_PX,GARDEN_ASSET_BOSS_CORE,32U,alpha_enabled);
    /* Three staggered local blasts followed by the delayed central bloom.
     * At most12 entries including the body/fragment paths are admitted. */
    for (unsigned i=0U;i<3U;i++) {
        unsigned born=6U+i*15U;
        if (age>=born && age<born+16U) {
            unsigned t=age-born;
            n=death_add(out,n,capacity,x+(i==0U ? -48:i==1U ? 48:0),
                        y+(i==2U ? 32:-16),GARDEN_ASSET_EXPLOSION0+t/4U,
                        32U-t,alpha_enabled);
        }
    }
    if (age>=60U) {
        unsigned t=age-60U;
        n=death_add(out,n,capacity,x,y,t<16U ? GARDEN_ASSET_EXPLOSION0+t/4U : GARDEN_ASSET_FX_RING48,
                    32U-t*31U/40U,alpha_enabled);
    }
    stage->stats.death_frames++;
    stage->stats.death_draws+=n-start;
    return n;
}

static void banner_draw(const GardenStageCache *view)
{
    uint16_t accent;
    garden_render_box(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,GARDEN_UI_PANEL);
    if (view->kind==GARDEN_STAGE_FOOTER) {
        garden_render_text_style(512,644,"DODGE THE COLOURED LIGHTS",GARDEN_TEXT_SMALL_WHITE);
        garden_render_text_style(512,664,"FOCUS. SURVIVE. BREAK THE PATTERN.",GARDEN_TEXT_SMALL_CYAN);
        return;
    }
    accent=view->pulse ? GARDEN_UI_CYAN : GARDEN_UI_MAGENTA;
    garden_render_box(512,640,8,12,accent);
    garden_render_box(932,640,8,12,accent);
    garden_render_text_style(536,638,"WARNING // BOSS INBOUND",GARDEN_TEXT_LARGE_WHITE);
    garden_render_text_style(536,658,view->demo ? "AUTO DEMO // TARGET SIGNAL LOCKED" :
                                                  "MANUAL FLIGHT // TARGET SIGNAL LOCKED",
                              GARDEN_TEXT_SMALL_CYAN);
    /* 10 small segments limit work and make every cache key correspond to
     * visible pixels. Both ends stay inside the fixed footer rectangle.
     */
    for (unsigned i=0U;i<10U;i++)
        garden_render_box(536+(int)i*38,671,32,3,
                          i<(unsigned)view->progress ? accent : GARDEN_UI_EDGE);
}

int garden_stage_draw(GardenStage *stage,const GardenGame *game,unsigned buffer_index)
{
    GardenStageCache next;
    GardenStageCache *cache;
    const GardenRenderStats *render;
    uint32_t before;
    if (!stage || !game) return -1;
    next=presentation(stage,game);
    if (buffer_index>=2U) {
        garden_stage_ui_reset(stage);
        render=garden_render_stats();
        before=render->errors+render->bad_targets+render->atlas_bounds;
        if (next.kind==GARDEN_STAGE_RESULTS) modal_draw(&next,game,1U);
        else banner_draw(&next);
        render=garden_render_stats();
        if (before!=render->errors+render->bad_targets+render->atlas_bounds) {
            garden_stage_ui_reset(stage);
            return -2;
        }
        stage->stats.banner_redraws++;
        return 1;
    }
    cache=&stage->cache[buffer_index];
    if (cache->valid && cache->kind==next.kind && cache->pulse==next.pulse &&
        cache->progress==next.progress && cache->score==next.score && cache->demo==next.demo)
        return 0;
    render=garden_render_stats();
    before=render->errors+render->bad_targets+render->atlas_bounds;
    if (next.kind==GARDEN_STAGE_RESULTS) {
        unsigned full=!cache->valid || cache->kind!=GARDEN_STAGE_RESULTS;
        /* Mark before drawing: a failed partial card must also be erased on
         * restart, and invalidated cache forces the next retry to full draw. */
        stage->modal_buffers|=(uint8_t)(1U<<buffer_index);
        if (full) { modal_draw(&next,game,1U);stage->stats.modal_redraws++; }
        else {
            if (cache->score!=next.score) modal_score(next.score);
            if (cache->progress!=next.progress || cache->demo!=next.demo) modal_prompt(&next);
            stage->stats.modal_updates++;
        }
    } else banner_draw(&next);
    render=garden_render_stats();
    if (before!=render->errors+render->bad_targets+render->atlas_bounds) {
        garden_stage_ui_reset(stage);
        return -2;
    }
    *cache=next;
    stage->stats.banner_redraws++;
    return 1;
}

const GardenStageStats *garden_stage_stats(const GardenStage *stage)
{
    return stage ? &stage->stats : 0;
}
