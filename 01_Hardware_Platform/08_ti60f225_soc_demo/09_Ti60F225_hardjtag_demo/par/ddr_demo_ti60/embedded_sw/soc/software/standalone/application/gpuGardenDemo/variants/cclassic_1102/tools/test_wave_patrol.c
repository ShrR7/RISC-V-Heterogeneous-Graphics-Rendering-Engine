/* Host-only contracts for periodic, bounded wave patrols. Actual production core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
#include "../src/garden_assets.h"
void patrol_ref_init(GardenGame *,uint32_t);
void patrol_ref_enemies(GardenGame *);
void patrol_ref_fire(GardenGame *,GardenEnemy *);
void patrol_ref_boss(GardenGame *);
void patrol_ref_laser(GardenGame *);
void patrol_ref_attack(GardenGame *,unsigned);
static unsigned checks,long_steps,finished_moves,attack_windows,large_volleys;
static unsigned direct_events,boss_snapshots,boss_steps,prefix_steps,stage_cases;
static unsigned age_saturated_steps,move_saturated_steps;
#define CHECK(c,m) do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static unsigned bullets(const GardenGame *g)
{unsigned n=0U;for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)n+=g->enemy_bullets[i].active!=0U;return n;}
static void clear_bullets(GardenGame *g)
{memset(g->enemy_bullets,0,sizeof(g->enemy_bullets));}
static void style(const GardenEnemyBullet *b,unsigned model)
{
 unsigned type=model?GARDEN_BULLET_DIAMOND:GARDEN_BULLET_DART,colour=model?4U:1U;
 CHECK(b->active && b->type==type && b->colour==colour,"model retains its one projectile type and colour");
 unsigned id=garden_bullet_precision_id(b->type,b->colour,b->angle);
 const garden_sprite_t *s=garden_asset(id);
 CHECK(s && s->width && s->height && s->offset+(unsigned)s->width*s->height*2U<=GARDEN_ATLAS_BYTES,"actual directional sprite remains in existing atlas");
 CHECK(garden_asset_is_enemy_bullet(id),"actual asset remains an enemy projectile");
}
static void direct_fire(void)
{
 for(unsigned seed=1;seed<=256U;seed++)for(unsigned model=0;model<2U;model++)for(unsigned shot=0;shot<8U;shot++){
  GardenGame a,b;garden_init(&a,seed*37U+shot);patrol_ref_init(&b,seed*37U+shot);
  a.enemies[0]=(GardenEnemy){.active=1,.type=model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,.hp=model?3800:300,.x=180000+(int)shot*25000,.y=120000,.aim_angle=(uint16_t)((seed*17U+shot*113U)&1023U)};b=a;
  enemy_fire(&a,&a.enemies[0]);patrol_ref_fire(&b,&b.enemies[0]);
  CHECK(!memcmp(&a,&b,sizeof(a)),"same-input enemy_fire including random history exactly matches parent");
  unsigned n=bullets(&a);CHECK(n==(model?8U:3U),"fixed per-model count retained");
  CHECK(a.enemies[0].recycle>=(model?120U:36U) && a.enemies[0].recycle<=(model?160U:60U),"existing low-density cooldown retained");
  for(unsigned i=0;i<n;i++)style(&a.enemy_bullets[i],model);
  direct_events++;
 }
}
static void unchanged_entry(void)
{
 for(unsigned seed=1;seed<=16U;seed++)for(unsigned model=0;model<2U;model++){
  GardenGame a,b;garden_init(&a,seed);patrol_ref_init(&b,seed);
  CHECK(spawn_enemy(&a,model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,100000+(int)seed*23000,-30000-(model?0:(int)seed*7000)),"reference entry admitted");b=a;
  unsigned stopped=0U;
  for(unsigned tick=0;tick<600U;tick++){
   GardenEnemy before=a.enemies[0];
   if(before.phase==3U && before.target_time==1){stopped=1U;break;}
   run_enemies(&a);patrol_ref_enemies(&b);
   CHECK(!memcmp(&a,&b,sizeof(a)),"first entrance/braking/attack/rest identical before first patrol");
   clear_bullets(&a);clear_bullets(&b);a.tick++;b.tick++;prefix_steps++;
  }
  CHECK(stopped,"first patrol reached within bounded initial interval");
 }
}
static int64_t cubic(unsigned k)
{return (int64_t)(120U-2U*k)*k*k;}
static void long_patrols(void)
{
 for(unsigned seed=1;seed<=8U;seed++)for(unsigned model=0;model<2U;model++){
  GardenGame g;garden_init(&g,seed*7919U);
  CHECK(spawn_enemy(&g,model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,100000+(int)(seed-1U)*60000,-30000-(model?0:(int)seed*15000)),"long-lived target admitted");
  GardenEnemy *e=&g.enemies[0];int hp=model?3607:263;e->hp=hp;
  unsigned seen_attack=0,window=0,window_tick=0,emissions=0,travel=0,travel_step=0,moves=0;
  int32_t sx=0,sy=0,tx=0,ty=0;
  for(unsigned tick=0;tick<80000U;tick++){
   GardenEnemy before=*e;run_enemies(&g);long_steps++;
   CHECK(e->active && e->type==(model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER) && e->hp==hp,"live target retains identity and already-damaged HP indefinitely");
   CHECK(e->age==(tick<65535U?tick+1U:65535U),"age saturates instead of wrapping or triggering top re-entry");
   CHECK(e->moves==before.moves || (before.moves<255U && e->moves==before.moves+1U),"move count never resets");
   CHECK(e->x>=100000 && e->x<=540000 && e->y<=280000,"normal entrance and patrol remain bounded");
   CHECK(abs32(e->x-before.x)<=16500 && abs32(e->y-before.y)<=16500,"no teleport or excessive single-step motion");
   CHECK(g.mode==GARDEN_MODE_WAVES && !g.wave_kills && !g.wave_next && !g.score,"movement grants no kill, score or stage transition");
   if(e->age==65535U)age_saturated_steps++;
   if(e->moves==255U)move_saturated_steps++;
   if(e->phase==2U && before.phase!=2U){
    seen_attack=1U;window=1U;window_tick=tick;emissions=0U;
    CHECK(e->target_time==(model?150:90),"existing attack window duration retained");
    CHECK(e->recycle>=(model?40U:8U) && e->recycle<=(model?64U:24U),"existing first-volley delay retained");
   }
   if(seen_attack)CHECK(e->phase!=0U && e->y>=90000,"once entered, target never vanishes above the screen or restarts entry");
   unsigned n=bullets(&g);
   if(n){
    CHECK(e->phase==2U && n==(model?8U:3U),"shots occur only in attack stage and retain fixed model count");
    if(!emissions)CHECK(tick-window_tick>=(model?40U:8U) && tick-window_tick<=(model?64U:24U),"actual first volley retains delay");
    emissions++;if(model)large_volleys++;
    for(unsigned i=0;i<n;i++)style(&g.enemy_bullets[i],model);
   }
   if(window && e->phase!=2U){
    CHECK(tick-window_tick==(model?150U:90U),"every completed attack window lasts exactly original duration");
    CHECK(model?emissions==1U:(emissions>=2U && emissions<=3U),"large window has exactly one eight-ring; small fan cadence retained");
    CHECK(e->phase==3U && e->target_time==(model?60:30),"post-attack rest retained");
    attack_windows++;window=0U;
   }
   if(e->phase==4U && before.phase!=4U){
    travel=1U;travel_step=0U;sx=before.x;sy=before.y;tx=e->target_x;ty=e->target_y;
    CHECK(before.phase==3U && before.target_time==1,"patrol starts only after complete recovery");
    CHECK(tx>=100000 && tx<=540000 && ty>=90000 && ty<=250000,"random target within upper combat arena");
    CHECK(sx<320000 ? tx>=360000:tx<=280000,"each patrol alternates horizontal side");
    CHECK(abs32(tx-sx)>=40000,"each patrol has visible horizontal travel");
   }
   if(travel){
    travel_step++;
    CHECK(travel_step<=40U,"patrol cannot stall or coast beyond one second");
    CHECK(e->x>=(sx<tx?sx:tx) && e->x<=(sx>tx?sx:tx) && e->y>=(sy<ty?sy:ty) && e->y<=(sy>ty?sy:ty),"movement does not overshoot its start/target rectangle");
    int64_t ideal_x=sx+(int64_t)(tx-sx)*cubic(travel_step)/64000;
    int64_t ideal_y=sy+(int64_t)(ty-sy)*cubic(travel_step)/64000;
    CHECK(llabs((long long)e->x-ideal_x)<100 && llabs((long long)e->y-ideal_y)<100,"remaining-weight recurrence follows smoothstep to subpixel precision");
    if(e->phase==4U){
     CHECK(e->vx==e->x-before.x && e->vy==e->y-before.y,"vx/vy retain actual step velocity for automatic avoidance prediction");
    }else{
     CHECK(travel_step==40U && e->phase==1U && e->target_time==30,"each move finishes in exactly40steps with30ticksettle");
     CHECK(e->x==tx && e->y==ty && !e->vx && !e->vy,"last step lands exactly and stops without coasting");
     travel=0U;moves++;finished_moves++;
    }
   }
   CHECK(e->phase<=4U && !g.dropped_enemy_bullets,"no escape stage or full-pool censorship");
   clear_bullets(&g);g.tick++;
  }
  CHECK(moves>=250U && e->age==65535U && e->moves==255U,"both types keep moving beyond old lifetime and both counter saturation points");
 }
}
static void boss_contracts(void)
{
 for(unsigned seed=1;seed<=32U;seed++)for(unsigned attack=1;attack<=3U;attack++)for(unsigned status=0;status<4U;status++){
  GardenGame a,b;garden_init(&a,seed);patrol_ref_init(&b,seed);a.mode=GARDEN_MODE_BOSS;
  a.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.status=(uint8_t)status,.volley_colour=3};b=a;
  if(attack==1U)boss_attack1(&a);else if(attack==2U)boss_attack2(&a);else boss_attack3(&a);patrol_ref_attack(&b,attack);
  CHECK(!memcmp(&a,&b,sizeof(a)),"all Boss attack generators remain identical for same input");boss_snapshots++;
 }
 for(unsigned seed=1;seed<=8U;seed++)for(unsigned band=0;band<3U;band++){
  GardenGame a,b;garden_init(&a,seed*65537U);patrol_ref_init(&b,seed*65537U);a.mode=GARDEN_MODE_BOSS;
  a.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.target_x=320000,.hp=band==0?50000:band==1?30000:10000,.max_hp=50000,.attack=1,.volley_colour=1};b=a;
  for(unsigned step=0;step<2000U;step++){
   run_laser(&a);run_boss(&a);patrol_ref_laser(&b);patrol_ref_boss(&b);
   CHECK(!memcmp(&a,&b,sizeof(a)),"paired real Boss/laser sequences retain calm cadence, RNG and anchors");
   clear_bullets(&a);clear_bullets(&b);a.tick++;b.tick++;boss_steps++;
  }
 }
}
static void strict_clear_and_controls(void)
{
 for(unsigned model=0;model<2U;model++){
  GardenGame g;garden_init(&g,7U+model);g.tick=1000U;g.wave_next=10U;g.wave_kills=9U;
  CHECK(spawn_enemy(&g,model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,320000,160000),"final unfinished target admitted");
  g.enemies[0].phase=3U;g.enemies[0].target_time=1;g.enemies[0].vx=g.enemies[0].vy=0;
  for(unsigned i=0;i<1700U;i++){run_enemies(&g);clear_bullets(&g);run_waves(&g);CHECK(g.mode==GARDEN_MODE_WAVES && g.enemies[0].active && g.wave_kills==9U,"old40second deadline cannot bypass final target");g.tick++;}
  GardenPlayerBullet shot={.active=1,.damage=(uint16_t)(g.enemies[0].hp-1)};
  CHECK(hit_target(&g,&shot,0U),"partial damage accepted");run_waves(&g);
  CHECK(g.enemies[0].active && g.enemies[0].hp==1 && g.mode==GARDEN_MODE_WAVES && g.wave_kills==9U,"partial hit neither clears nor resets target");
  shot.damage=1U;CHECK(hit_target(&g,&shot,0U),"final normal hit accepted");run_waves(&g);
  CHECK(!g.enemies[0].active && g.wave_kills==10U && g.mode==GARDEN_MODE_BOSS && g.boss.active && g.boss.move_ticks==100U,"only final real kill opens unchanged Boss entrance");stage_cases++;
 }
 {
  GardenGame g;garden_init(&g,9U);g.tick=2000U;g.wave_next=10U;g.wave_kills=10U;spawn_enemy(&g,GARDEN_ENEMY_STOPPER,320000,160000);run_waves(&g);
  CHECK(g.mode==GARDEN_MODE_WAVES,"actual active-slot scan prevents an inconsistent-count early transition");stage_cases++;
  g.enemies[0].active=0;g.wave_next=9U;run_waves(&g);CHECK(g.mode==GARDEN_MODE_WAVES,"missing scheduled target cannot be skipped");stage_cases++;
 }
 {
  GardenGame g;garden_init(&g,99U);g.tick=500U;g.wave_next=10U;g.demo_mode=0U;
  spawn_enemy(&g,GARDEN_ENEMY_STOPPER,200000,140000);g.enemies[0].phase=3U;g.enemies[0].target_time=1;g.enemies[0].vx=g.enemies[0].vy=0;run_enemies(&g);
  CHECK(g.enemies[0].phase==4U,"control fixture in active patrol");
  garden_step(&g,GARDEN_INPUT_PAUSE);GardenEnemy frozen=g.enemies[0];unsigned tick=g.tick;
  for(unsigned i=0;i<50U;i++){garden_step(&g,0U);CHECK(g.paused && g.tick==tick && !memcmp(&frozen,&g.enemies[0],sizeof(frozen)),"pause freezes patrol coordinate/timer/age exactly");}
  garden_step(&g,GARDEN_INPUT_PAUSE);CHECK(!g.paused && g.tick==tick+1U && g.enemies[0].age==frozen.age+1U && g.enemies[0].phase==4U,"resume advances original trip without teleport or re-entry");
  g.reward.high_score=1234U;garden_step(&g,GARDEN_INPUT_RESTART);
  CHECK(g.tick==0U && g.wave_next==0U && g.wave_kills==0U && !wave_enemies_alive(&g) && g.reward.high_score==1234U,"explicit restart clears patrol normally and preserves best score");stage_cases++;
 }
}
int main(void)
{
 direct_fire();unchanged_entry();long_patrols();boss_contracts();strict_clear_and_controls();
 printf("PATROL long_steps=%u moves=%u windows=%u large_volleys=%u age_saturated_steps=%u move_saturated_steps=%u\n",long_steps,finished_moves,attack_windows,large_volleys,age_saturated_steps,move_saturated_steps);
 printf("CONTROL direct_events=%u prefix_steps=%u boss_snapshots=%u boss_steps=%u stage_cases=%u\n",direct_events,prefix_steps,boss_snapshots,boss_steps,stage_cases);
 printf("PASS wave_patrol checks=%u game_bytes=%u\n",checks,(unsigned)sizeof(GardenGame));return 0;
}

