/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_actor_ui.h"
#include "garden_assets.h"
#include <string.h>

void garden_actor_ui_reset(GardenActorUi *ui)
{ if(ui) memset(ui,0,sizeof(*ui)); }

void garden_actor_ui_observe(GardenActorUi *ui,const GardenGame *game)
{
    int target;
    if(!ui || !game) return;
    if(!ui->initialized || game->tick<ui->last_tick ||
       (ui->last_mode!=GARDEN_MODE_WAVES && game->mode==GARDEN_MODE_WAVES)) {
        ui->initialized=1U;ui->last_tick=game->tick;ui->last_mode=game->mode;
        ui->bank=0;ui->stats.bank=0;return;
    }
    if(game->tick==ui->last_tick || game->paused) return;
    ui->last_tick=game->tick;ui->last_mode=game->mode;
    target=!game->player.active || game->player.respawning ? 0:
           game->player.move_x<0 ? -2:game->player.move_x>0 ? 2:0;
    if(!game->player.active || game->player.respawning) {
        if(ui->bank) ui->stats.bank_changes++;
        ui->bank=0;
    } else if(ui->bank<target) { ui->bank++;ui->stats.bank_changes++; }
    else if(ui->bank>target) { ui->bank--;ui->stats.bank_changes++; }
    ui->stats.bank=ui->bank;
}

unsigned garden_actor_ui_player_asset(const GardenActorUi *ui,const GardenGame *game)
{
    int bank=ui ? ui->bank:0;
    unsigned phase=game ? (game->tick/3U)%3U:0U;
    if(bank<-2 || bank>2) bank=0;
    return GARDEN_DRAW_BANK_BASE|((unsigned)(bank+2)<<2)|phase;
}

static unsigned actor_visible(unsigned asset,int32_t x,int32_t y)
{
    const garden_sprite_t *sprite=garden_asset(asset);
    if(!sprite || !sprite->width || !sprite->height) return 0U;
    x=x/GARDEN_GRAIN-sprite->anchor_x;
    y=y/GARDEN_GRAIN-sprite->anchor_y;
    return x<GARDEN_WIDTH && y<GARDEN_HEIGHT &&
           x+(int32_t)sprite->width>0 && y+(int32_t)sprite->height>0;
}

unsigned garden_actor_ui_bar_offset(unsigned asset)
{
    const garden_sprite_t *sprite=garden_asset(asset);
    return (sprite && sprite->anchor_y>0 ? (unsigned)sprite->anchor_y:0U)+
           GARDEN_ACTOR_UI_BAR_HEIGHT+GARDEN_ACTOR_UI_BAR_GAP;
}

unsigned garden_actor_ui_bar_geometry(const GardenGame *game,unsigned slot,GardenDraw *draw)
{
    int32_t hp,max_hp,x,y;
    unsigned boss=slot==GARDEN_MAX_ENEMIES,width,fill,inner;
    if(!game || !draw || slot>GARDEN_MAX_ENEMIES) return 0U;
    if(boss) {
        if(!game->boss.active || game->boss.hp<=0 || game->boss.max_hp<=0 ||
           !actor_visible(GARDEN_ASSET_BOSS1,game->boss.x,game->boss.y)) return 0U;
        hp=game->boss.hp;max_hp=game->boss.max_hp;
        x=game->boss.x/GARDEN_GRAIN;
        y=game->boss.y/GARDEN_GRAIN-(int32_t)garden_actor_ui_bar_offset(GARDEN_ASSET_BOSS1);
    } else {
        const GardenEnemy *enemy=&game->enemies[slot];
        if(!enemy->active || enemy->hp<=0 ||
           (enemy->type!=GARDEN_ENEMY_DARTER && enemy->type!=GARDEN_ENEMY_STOPPER)) return 0U;
        if(!actor_visible(enemy->type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1:
                          GARDEN_ASSET_STOPPER1,enemy->x,enemy->y)) return 0U;
        hp=enemy->hp;
        /* These are the original fixed spawn HP values, unchanged in this
         * edition. This must change if future spawn rules vary enemy HP. */
        max_hp=enemy->type==GARDEN_ENEMY_DARTER ? 300:3800;
        x=enemy->x/GARDEN_GRAIN;
        y=enemy->y/GARDEN_GRAIN-(int32_t)garden_actor_ui_bar_offset(
            enemy->type==GARDEN_ENEMY_DARTER ? GARDEN_ASSET_DARTER1:GARDEN_ASSET_STOPPER1);
    }
    width=boss ? GARDEN_ACTOR_UI_BOSS_BAR_WIDTH:GARDEN_ACTOR_UI_ENEMY_BAR_WIDTH;
    inner=width-2U;
    if(hp>max_hp) hp=max_hp;
    /* Round up: positive HP always keeps a visible 1px sliver. HP=0 has no
     * live bar. uint64_t avoids overflow for malformed/large HP inputs. */
    fill=(unsigned)(((uint64_t)(uint32_t)hp*inner+(uint32_t)max_hp-1U)/(uint32_t)max_hp);
    x-=(int32_t)width/2;
    if(x<0) x=0;
    if(x>GARDEN_WIDTH-(int32_t)width) x=GARDEN_WIDTH-(int32_t)width;
    if(y<0) y=0;
    if(y>GARDEN_HEIGHT-(int32_t)GARDEN_ACTOR_UI_BAR_HEIGHT) y=GARDEN_HEIGHT-(int32_t)GARDEN_ACTOR_UI_BAR_HEIGHT;
    *draw=(GardenDraw){(int16_t)x,(int16_t)y,
        (uint16_t)(GARDEN_DRAW_BAR_BASE|(boss ? GARDEN_DRAW_BAR_BOSS:0U)|fill),32U};
    return 1U;
}

unsigned garden_actor_ui_bar_count(const GardenGame *game)
{
    unsigned n=0U;GardenDraw draw;
    if(!game) return 0U;
    for(unsigned slot=0U;slot<=GARDEN_MAX_ENEMIES;slot++) n+=garden_actor_ui_bar_geometry(game,slot,&draw);
    return n;
}

unsigned garden_actor_ui_apply(GardenActorUi *ui,const GardenGame *game,GardenDraw *draws,unsigned count)
{
    if(!ui || !game || !draws) return 0U;
    if(count>GARDEN_DRAW_MAX) count=GARDEN_DRAW_MAX;
    for(unsigned i=count;i>0U;i--) {
        GardenDraw *d=&draws[i-1U];
        if((d->asset&0xE000U)==GARDEN_DRAW_BANK_BASE &&
           d->x==game->player.x/GARDEN_GRAIN && d->y==game->player.y/GARDEN_GRAIN) return 0U;
        if((d->asset==GARDEN_ASSET_PLAYER || d->asset==GARDEN_ASSET_PLAYER_FRAME1 ||
            d->asset==GARDEN_ASSET_PLAYER_FRAME2) &&
           d->x==game->player.x/GARDEN_GRAIN && d->y==game->player.y/GARDEN_GRAIN) {
            d->asset=(uint16_t)garden_actor_ui_player_asset(ui,game);
            ui->stats.player_frames++;
            if(ui->bank) ui->stats.bank_frames++;
            return 1U;
        }
    }
    return 0U;
}

unsigned garden_actor_ui_append_bars(GardenActorUi *ui,const GardenGame *game,
                                    GardenDraw *draws,unsigned count,unsigned capacity)
{
    GardenDraw draw,accepted[GARDEN_MAX_ENEMIES+1U];
    unsigned bars=0U,insert;
    if(!ui || !game || (!draws && capacity)) return count;
    if(capacity>GARDEN_DRAW_MAX) capacity=GARDEN_DRAW_MAX;
    if(count>capacity) count=capacity;
    ui->stats.bars=ui->stats.enemy_bars=ui->stats.boss_bars=0U;
    for(unsigned slot=0U;slot<=GARDEN_MAX_ENEMIES;slot++) {
        if(!garden_actor_ui_bar_geometry(game,slot,&draw)) continue;
        if(count+bars>=capacity) { ui->stats.bar_drop_total++;continue; }
        accepted[bars++]=draw;ui->stats.bars++;ui->stats.bar_frames++;
        if(slot==GARDEN_MAX_ENEMIES) ui->stats.boss_bars++;else ui->stats.enemy_bars++;
    }
    /* HP UI is above the actors but under actual HP-loss labels and the true
     * collision marker. Do not bury either readability cue under a bar. */
    insert=count;
    for(unsigned i=0U;i<count;i++) {
        unsigned asset=draws[i].asset;
        if((asset&GARDEN_DRAW_SCORE_BASE) || asset==GARDEN_ASSET_FOCUS_VULNERABLE ||
           asset==GARDEN_ASSET_FOCUS_GRACE) { insert=i;break; }
    }
    if(bars) {
        memmove(draws+insert+bars,draws+insert,(count-insert)*sizeof(*draws));
        memcpy(draws+insert,accepted,bars*sizeof(*draws));
    }
    return count+bars;
}
const GardenActorUiStats *garden_actor_ui_stats(const GardenActorUi *ui)
{ return ui ? &ui->stats:0; }
