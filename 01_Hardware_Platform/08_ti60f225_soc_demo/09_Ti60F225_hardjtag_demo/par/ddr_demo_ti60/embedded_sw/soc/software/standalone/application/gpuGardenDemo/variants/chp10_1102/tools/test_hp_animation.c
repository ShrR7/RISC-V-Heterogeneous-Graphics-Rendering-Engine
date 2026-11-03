/* Actual production animation contracts; no GPU/board/clock simulation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_animation.h"
#include "garden_assets.h"
#include "garden_motion.h"
unsigned garden_enemy_scan_limit=192U;
static GardenGame game,saved;
static unsigned checks,nonfatal;
#define CHECK(c,m) do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static void observe(GardenAnimation *a)
{saved=game;garden_animation_observe(a,&game);CHECK(!memcmp(&saved,&game,sizeof(game)),"observer never changes HP/core");}
static unsigned apply(GardenAnimation *a,GardenDraw *d,unsigned n,unsigned cap,unsigned alpha)
{saved=game;unsigned result=garden_animation_apply(a,&game,d,n,cap,alpha);CHECK(!memcmp(&saved,&game,sizeof(game)),"render transform never changes HP/core");return result;}
static const GardenDraw base[5]={{320,400,GARDEN_ASSET_PLAYER,32},
 {320,400,GARDEN_ASSET_FOCUS_GRACE,32},{180,240,GARDEN_ASSET_DART_BASE,32},
 {300,80,GARDEN_DRAW_BAR_BASE|24U,32},{300,50,GARDEN_DRAW_SCORE_BASE|95U,32}};
static void motion_contract(void)
{
 static GardenMotion motion;garden_motion_reset(&motion);memset(&game,0,sizeof(game));
 game.tick=1U;game.mode=GARDEN_MODE_BOSS;game.lives=10U;game.player.active=1U;
 game.player.x=320000;game.player.y=400000;garden_motion_observe(&motion,&game);
 game.tick++;game.lives=9U;game.player.x=330000;garden_motion_observe(&motion,&game);
 CHECK(motion.pilot.smooth && motion.pilot.previous_x==320*16 && motion.pilot.current_x==330*16,"nonfatal damage preserves normal pilot interpolation");
 /* Isolate the HP transition from the independent existing mode-epoch reset.
  * The real core also changes mode, which remains an additional reset reason. */
 game.tick++;game.lives=0U;game.player.x=340000;garden_motion_observe(&motion,&game);
 CHECK(!motion.pilot.smooth && motion.pilot.previous_x==motion.pilot.current_x,"final zeroHP resets interpolation even before a mode observation");
}
int main(void)
{
 GardenAnimation a;garden_animation_reset(&a);memset(&game,0,sizeof(game));
 game.tick=100U;game.mode=GARDEN_MODE_BOSS;game.lives=10U;
 game.player.active=1U;game.player.x=320000;game.player.y=400000;observe(&a);
 for(unsigned hp=9U;hp;hp--) {
  game.tick++;game.lives=(uint8_t)hp;game.player.grace=20U;
  game.effects[0]=(GardenEffect){.active=1U,.age=0U,.big=1U,.x=game.player.x,.y=game.player.y};
  observe(&a);CHECK(!a.death_active && !a.stats.death_triggers,"nonfatal HP damage never becomes an aircraft death");
  GardenAnimation same=a;observe(&a);CHECK(!memcmp(&a,&same,sizeof(a)),"same-tick damage observation idempotent");
  for(unsigned alpha=0U;alpha<2U;alpha++) {
   GardenDraw d[14];memcpy(d,base,sizeof(base));d[5]=(GardenDraw){12,13,0xBEEFU,31U};
   CHECK(apply(&a,d,5U,5U,alpha)==5U,"nonfatal damage adds no death draws even with full capacity");
   CHECK(d[5].asset==0xBEEFU && d[5].x==12,"nonfatal build respects list canary");
   for(unsigned i=0U;i<5U;i++) {
    CHECK(d[i].x==base[i].x && d[i].y==base[i].y && d[i].asset==base[i].asset,"nonfatal damage never death-shakes actors/bullets/bars/damage");
    CHECK(d[i].alpha==(i==1U && alpha ? 16U+((game.tick&7U)*16U)/7U:32U),"existing real invulnerability marker pulse and alpha-off fallback preserved");
   }
   CHECK(!a.stats.fragments && !a.stats.camera_x && !a.stats.camera_y && !a.stats.fragment_dropped,"nonfatal hits produce no fragment/camera work or admission loss");
  }
  nonfatal++;
 }
 game.tick++;game.lives=0U;game.player.active=0U;game.player.grace=0U;game.mode=GARDEN_MODE_GAMEOVER;
 game.effects[0]=(GardenEffect){.active=1U,.age=0U,.big=1U,.x=game.player.x+10000,.y=game.player.y-12000};
 observe(&a);CHECK(a.death_active && a.stats.death_triggers==1U,"only final1-to0HP starts real death burst");
 CHECK(a.death_x==330 && a.death_y==388,"death fragments use genuine fresh effect position");
 for(unsigned alpha=0U;alpha<2U;alpha++) {
  GardenDraw d[14];memcpy(d,base,sizeof(base));d[13]=(GardenDraw){12,13,0xBEEFU,31U};
  unsigned n=apply(&a,d,5U,13U,alpha);CHECK(n==13U && a.stats.fragments==8U,"true death retains all eight available fragments");
  CHECK(d[13].asset==0xBEEFU,"true death bounded admission canary");
  CHECK(a.stats.camera_x==4 && !a.stats.camera_y,"true GameOver death retains first bounded impact shake");
  for(unsigned i=0U;i<5U;i++){unsigned at=i ? i+8U:i;CHECK(d[at].x==base[i].x+4 && d[at].y==base[i].y && d[at].asset==base[i].asset,"real death camera moves all original world entries uniformly");}
 }
 game.paused=1U;observe(&a);GardenAnimation paused=a;game.tick++;observe(&a);
 CHECK(a.death_age==paused.death_age,"pause freezes genuine death lifetime");game.paused=0U;
 for(unsigned age=1U;age<GARDEN_ANIMATION_DEATH_TICKS;age++) {
  if(age>1U)game.tick++;
  observe(&a);garden_animation_set_fraction(&a,192U);
  GardenDraw d[13];memcpy(d,base,sizeof(base));CHECK(apply(&a,d,5U,13U,1U)==13U,"genuine death fragments survive their normal lifetime");
  CHECK(a.death_age==age,"real death age advances once per logic observation");
  if(age>=GARDEN_ANIMATION_SHAKE_TICKS)CHECK(!a.stats.camera_x && !a.stats.camera_y,"death camera settles while fragments continue");
 }
 game.tick++;observe(&a);GardenDraw d[13];memcpy(d,base,sizeof(base));
 CHECK(apply(&a,d,5U,13U,1U)==5U && !a.death_active,"true death fragments expire normally");
 game.tick=0U;game.mode=GARDEN_MODE_WAVES;game.lives=10U;game.player.active=1U;observe(&a);
 CHECK(!a.death_active && !a.fraction_q8 && a.last_lives==10U,"restart clears envelope and restores observer HP epoch");
 motion_contract();
 printf("PASS hp_animation checks=%u nonfatal_hp_steps=%u final_death=1 grace_pulse_unchanged=1 board_acceptance=0\n",checks,nonfatal);return 0;
}
