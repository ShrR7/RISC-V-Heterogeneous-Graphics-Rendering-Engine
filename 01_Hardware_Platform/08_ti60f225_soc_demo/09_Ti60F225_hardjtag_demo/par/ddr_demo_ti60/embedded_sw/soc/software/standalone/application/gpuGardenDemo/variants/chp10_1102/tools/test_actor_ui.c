/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual actor-state/HP presentation contracts, no GPU/MMIO or timing claims.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "garden_actor_ui.h"
#include "garden_assets.h"
static unsigned checks;
#define CHECK(c,m) do { checks++; if(!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m);exit(2); } } while(0)
static GardenGame game,unchanged;
static GardenDraw draws[GARDEN_DRAW_MAX+1U];
static void sample(GardenActorUi *ui)
{ unchanged=game;garden_actor_ui_observe(ui,&game);CHECK(!memcmp(&unchanged,&game,sizeof(game)),"bank observation never alters gameplay"); }
static void poses(void)
{
    GardenActorUi ui;garden_actor_ui_reset(&ui);memset(&game,0,sizeof(game));
    game.player.active=1;game.player.x=100000;game.player.y=300000;game.player.move_x=1;
    sample(&ui);CHECK(ui.bank==0,"first sample starts neutral independent of initial velocity");
    for(unsigned tick=1;tick<40;tick++) {
        game.tick=tick;game.player.move_x=tick<4 ? 1:tick<10 ? -1:0;
        int old=ui.bank;sample(&ui);
        CHECK(ui.bank>=-2 && ui.bank<=2 && abs(ui.bank-old)<=1,"five bank poses progress by at most one logical step");
        int stable=ui.bank;sample(&ui);CHECK(ui.bank==stable,"same tick is idempotent");
        unsigned asset=garden_actor_ui_player_asset(&ui,&game);
        CHECK((asset&0xE000U)==GARDEN_DRAW_BANK_BASE && ((asset&31U)>>2)==(unsigned)(ui.bank+2) &&
              (asset&3U)==(game.tick/3U)%3U,"bank encoding binds real thruster frame and bounded pose");
    }
    CHECK(ui.bank==0,"stationary player eases back to neutral");
    game.player.move_x=-1;game.tick++;sample(&ui);CHECK(ui.bank==-1,"leftward movement banks left");
    game.paused=1;game.tick++;sample(&ui);CHECK(ui.bank==-1,"pause freezes banking even if a malformed tick advances");
    game.paused=0;sample(&ui);CHECK(ui.bank==-2,"resume advances one pose");
    game.player.respawning=5;game.tick++;sample(&ui);CHECK(ui.bank==0,"respawn returns body to neutral without modifying centre");
    game.player.respawning=0;game.player.move_x=1;game.tick++;sample(&ui);game.tick++;sample(&ui);CHECK(ui.bank==2,"full right bank observed");
    game.tick=0;sample(&ui);CHECK(ui.bank==0,"restart tick rollback clears bank");
    ui.bank=-2;game.mode=GARDEN_MODE_BOSS;game.tick=2;sample(&ui);game.mode=GARDEN_MODE_WAVES;game.tick=3;sample(&ui);
    CHECK(ui.bank==0,"return to waves epoch resets bank");
    draws[0]=(GardenDraw){100,300,GARDEN_ASSET_PLAYER,12};
    draws[1]=(GardenDraw){100,300,GARDEN_ASSET_PLAYER_FRAME1,32};
    draws[2]=(GardenDraw){100,300,GARDEN_ASSET_FOCUS_VULNERABLE,32};
    GardenDraw old[3];memcpy(old,draws,sizeof(old));unchanged=game;
    CHECK(garden_actor_ui_apply(&ui,&game,draws,3)==1,"actual player entry found");
    CHECK(!memcmp(&old[0],&draws[0],sizeof(GardenDraw)) && !memcmp(&old[2],&draws[2],sizeof(GardenDraw)),"coincident old trail and true marker remain intact");
    CHECK(draws[1].x==old[1].x && draws[1].y==old[1].y && draws[1].alpha==old[1].alpha && draws[1].asset==garden_actor_ui_player_asset(&ui,&game),"only actual last player asset changes");
    CHECK(!memcmp(&game,&unchanged,sizeof(game)),"draw composition cannot move the player's real hitbox");
    CHECK(!garden_actor_ui_apply(&ui,&game,draws,3),"already banked body is not rewritten or counted twice");
    puts("PASS: five progressive bank poses, true anchor unchanged, same-tick/pause/respawn/restart contracts, old trail/marker protection");
}
static void health(void)
{
    GardenActorUi ui;GardenDraw bar;garden_actor_ui_reset(&ui);memset(&game,0,sizeof(game));
    for(unsigned type=1;type<=2;type++) {
        GardenEnemy *e=&game.enemies[0];e->active=1;e->type=(uint8_t)type;e->x=100000;e->y=200000;
        unsigned maximum=type==1 ? 300U:3800U;
        for(unsigned hp=1;hp<=maximum;hp++) {
            e->hp=(int32_t)hp;unchanged=game;
            CHECK(garden_actor_ui_bar_geometry(&game,0,&bar),"every live positive-HP enemy has a bar");
            CHECK(bar.x==80 && bar.y==(type==1 ? 159:144) && bar.alpha==32,"enemy bar sits above actual bitmap and uses opaque 40x8 geometry");
            CHECK((bar.asset&0x3FFFU)==(hp*38U+maximum-1U)/maximum,"true enemy HP, rounded up only to preserve visible positive sliver");
            CHECK(!memcmp(&game,&unchanged,sizeof(game)),"bar reads but never writes HP/gameplay");
        }
        e->hp=(int32_t)maximum+999;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar) && (bar.asset&255U)==38U,"HP above spawn maximum clamps full");
        e->hp=0;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"dead enemy bar disappears");
        e->hp=-1;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"negative HP is not displayed");
        e->hp=1;e->x=-900000;e->y=-900000;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"fully offscreen upper-left body has no isolated clamped bar");
        e->x=990000;e->y=990000;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"fully offscreen bottom-right body has no isolated clamped bar");
        e->x=-5000;e->y=10000;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar) && bar.x==0 && bar.y==0,"partially entering upper-left actor gets one clamped bar");
        e->x=650000;e->y=type==1 ? 510000:525000;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar) && bar.x==600 && bar.y==469,"partially visible bottom-right actor bar remains in field");
        const garden_sprite_t *sprite=garden_asset(type==1 ? GARDEN_ASSET_DARTER1:GARDEN_ASSET_STOPPER1);
        e->x=100000;e->y=((int32_t)sprite->anchor_y-(int32_t)sprite->height)*GARDEN_GRAIN;
        CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"exactly-above sprite bottom touching y=0 without area intersection has no bar");
        e->y+=GARDEN_GRAIN;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar),"first visible sprite row admits top-clamped bar");
        e->y=200000;e->x=((int32_t)sprite->anchor_x-(int32_t)sprite->width)*GARDEN_GRAIN;
        CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"exactly-left sprite edge touching x=0 has no bar");
        e->x+=GARDEN_GRAIN;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar),"first visible sprite column admits left-clamped bar");
        e->x=(GARDEN_WIDTH+(int32_t)sprite->anchor_x)*GARDEN_GRAIN;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"exactly-right sprite start touching field edge has no bar");
        e->x-=GARDEN_GRAIN;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar),"last visible sprite column keeps bar");
        e->x=100000;e->y=(GARDEN_HEIGHT+(int32_t)sprite->anchor_y)*GARDEN_GRAIN;CHECK(!garden_actor_ui_bar_geometry(&game,0,&bar),"exactly-below sprite start touching field edge has no bar");
        e->y-=GARDEN_GRAIN;CHECK(garden_actor_ui_bar_geometry(&game,0,&bar),"last visible sprite row keeps bar");
    }
    game.enemies[0].type=99;CHECK(!garden_actor_ui_bar_count(&game),"unknown live enemy type never invents maximum HP");
    game.boss.active=1;game.boss.max_hp=50000;game.boss.x=320000;game.boss.y=110000;
    for(unsigned hp=1;hp<=50000;hp++) {
        game.boss.hp=(int32_t)hp;
        CHECK(garden_actor_ui_bar_geometry(&game,8,&bar),"each live Boss HP value yields bar");
        CHECK(bar.x==256 && bar.y==40 && (bar.asset&0x0100U) && (bar.asset&255U)==(hp*126U+49999U)/50000U,"true Boss HP fill in 128x8 bar above model");
    }
    game.boss.max_hp=INT_MAX;game.boss.hp=INT_MAX;CHECK(garden_actor_ui_bar_geometry(&game,8,&bar) && (bar.asset&255U)==126U,"maximum signed HP ratio cannot overflow");
    game.boss.hp=1;CHECK(garden_actor_ui_bar_geometry(&game,8,&bar) && (bar.asset&255U)==1U,"one remaining HP never vanishes prematurely");
    const garden_sprite_t *boss_sprite=garden_asset(GARDEN_ASSET_BOSS1);
    game.boss.y=((int32_t)boss_sprite->anchor_y-(int32_t)boss_sprite->height)*GARDEN_GRAIN;
    CHECK(!garden_actor_ui_bar_geometry(&game,8,&bar),"fully offscreen Boss has no orphan HP strip");
    game.boss.y+=GARDEN_GRAIN;CHECK(garden_actor_ui_bar_geometry(&game,8,&bar) && bar.y==0,"first visible Boss row shows clamped bar");
    game.boss.y=110000;
    game.boss.max_hp=0;CHECK(!garden_actor_ui_bar_geometry(&game,8,&bar),"invalid zero maxHP omitted");
    CHECK(!garden_actor_ui_bar_geometry(&game,9,&bar) && !garden_actor_ui_bar_geometry(NULL,0,&bar) && !garden_actor_ui_bar_geometry(&game,0,NULL),"invalid target/null inputs rejected");
    puts("PASS: every enemy/Boss HP ratio including overkill, one-HP sliver, malformed maximum and field-edge placement");
}
static void budgets(void)
{
    GardenActorUi ui;garden_actor_ui_reset(&ui);memset(&game,0,sizeof(game));
    for(unsigned slot=0;slot<8;slot++) game.enemies[slot]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.hp=300,.x=100000,.y=200000};
    game.boss=(GardenBoss){.active=1,.hp=50000,.max_hp=50000,.x=320000,.y=100000};
    CHECK(garden_actor_ui_bar_count(&game)==9,"all eight visible live enemies plus Boss reserve nine slots");
    draws[0]=(GardenDraw){100,100,GARDEN_ASSET_BOSS1,32};
    draws[1]=(GardenDraw){120,80,0x805FU,32};
    draws[2]=(GardenDraw){100,300,GARDEN_ASSET_FOCUS_VULNERABLE,32};
    GardenDraw label=draws[1],marker=draws[2];
    unsigned n=garden_actor_ui_append_bars(&ui,&game,draws,3,GARDEN_DRAW_MAX);
    CHECK(n==12 && ui.stats.enemy_bars==8 && ui.stats.boss_bars==1 && ui.stats.bars==9,"normal budget draws every living actor bar");
    CHECK(!memcmp(&label,&draws[10],sizeof(label)) && !memcmp(&marker,&draws[11],sizeof(marker)),"bars paint below actual damage labels and collision marker");
    draws[0]=label;draws[1]=marker;draws[6]=(GardenDraw){-123,456,789,31};GardenDraw sentinel=draws[6];
    n=garden_actor_ui_append_bars(&ui,&game,draws,2,6);
    CHECK(n==6 && ui.stats.bars==4 && ui.stats.bar_drop_total==5,"undersized caller budget records unshown bars rather than writing beyond capacity");
    CHECK(!memcmp(&sentinel,&draws[6],sizeof(sentinel)) && !memcmp(&label,&draws[4],sizeof(label)) && !memcmp(&marker,&draws[5],sizeof(marker)),"capacity canary and high-priority tail preserved");
    n=garden_actor_ui_append_bars(&ui,&game,NULL,0,0);CHECK(n==0 && ui.stats.bars==0 && ui.stats.bar_drop_total==14,"zero capacity is safe and all omitted bars counted");
    CHECK(garden_actor_ui_stats(&ui)==&ui.stats && garden_actor_ui_stats(NULL)==NULL,"stats observer is read-only and null safe");
    puts("PASS: full 9-bar budget, damage/marker ordering, clipped capacity and fixed-array canary");
}
int main(void) { poses();health();budgets();printf("PASS: actor UI host contract checks=%u\n",checks);return 0; }
