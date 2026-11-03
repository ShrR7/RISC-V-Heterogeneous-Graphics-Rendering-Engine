/* Host-only real production core cadence/position contracts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
void calm_ref_init(GardenGame *,uint32_t);
void calm_ref_step(GardenGame *,uint16_t);
void calm_ref_boss(GardenGame *);
void calm_ref_laser(GardenGame *);
void calm_ref_start(GardenGame *);
void calm_ref_enemies(GardenGame *);
void calm_ref_attack(GardenGame *,unsigned);
static unsigned checks,rest_cases,holds,moves,sequence_steps,laser_ticks[4],cross_rest,wait_cases,boss_snapshots,wave_steps;
#define CHECK(c,m)do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static void setup(GardenGame *g,unsigned seed,unsigned band)
{garden_init(g,seed);g->mode=GARDEN_MODE_BOSS;g->boss=(GardenBoss){.active=1,.x=320000,.y=120000,.target_x=320000,.hp=band==0?50000:band==1?30000:10000,.max_hp=50000,.attack=1,.volley_colour=1};}
static void normalized_equal(const GardenGame *a,const GardenGame *b)
{
 GardenGame x=*a,y=*b;
 x.boss.x=y.boss.x=0;x.boss.target_x=y.boss.target_x=0;
 for(unsigned i=0;i<2U;i++)x.laser.x[i]=y.laser.x[i]=0;
 for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++){
  CHECK(x.enemy_bullets[i].active==y.enemy_bullets[i].active,"projectile allocation count/order unchanged");
  if(x.enemy_bullets[i].active){x.enemy_bullets[i].x-=a->boss.x;y.enemy_bullets[i].x-=b->boss.x;}
 }
 CHECK(!memcmp(&x,&y,sizeof(x)),"all state/PRNG/timers/ledger/projectile properties identical except permitted horizontal translation");
}
static void anchored(const GardenGame *g)
{
 if(g->laser.phase!=GARDEN_LASER_OFF){
  CHECK(g->boss.x==g->laser.x[0]-GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN,"body stays anchored to exact left laser muzzle");
  CHECK(g->boss.y==g->laser.y-GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN,"vertical laser anchor unchanged");
  CHECK(g->laser.x[1]==g->boss.x+GARDEN_BOSS_MUZZLE_RIGHT_X_PX*GARDEN_GRAIN,"right laser rail remains attached");
 }
}
static void direct_rest(void)
{
 for(unsigned seed=1;seed<=1024U;seed++)for(unsigned band=0;band<3U;band++)for(unsigned completed=1;completed<=9U;completed++){
  GardenGame a,b;setup(&a,seed,band);a.boss.rhythm_state=GARDEN_BOSS_REST;a.boss.rhythm_ticks=1U;a.rhythm.bursts_completed=completed;b=a;
  int32_t old_x=a.boss.x;run_boss(&a);calm_ref_boss(&b);
  CHECK(a.boss.rhythm_state==GARDEN_BOSS_TRANSIT && a.boss.move_ticks>=32U && a.boss.move_ticks<=56U,"REST retains original32..56tick transit/hold slot");
  CHECK(a.boss.move_ticks==b.boss.move_ticks && a.rng==b.rng,"REST consumes original random draws and movement duration");
  CHECK(a.boss.target_x==(completed%3U?old_x:b.boss.target_x),"only every third completed train keeps random movement destination");
  normalized_equal(&a,&b);rest_cases++;
 }
}
static void sequences(void)
{
 for(unsigned seed=1;seed<=16U;seed++)for(unsigned band=0;band<3U;band++){
  GardenGame a,b;garden_init(&a,seed);a.wave_next=GARDEN_WAVE_ENEMIES;a.wave_kills=GARDEN_WAVE_ENEMIES;b=a;start_boss(&a);calm_ref_start(&b);
  CHECK(!memcmp(&a,&b,sizeof(a)),"initial Boss100tick entrance setup byte-identical");
  a.boss.hp=b.boss.hp=band==0?50000:band==1?30000:10000;
  for(unsigned tick=1;tick<=3000U;tick++){
   unsigned rest_end=a.boss.rhythm_state==GARDEN_BOSS_REST && a.boss.rhythm_ticks==1U;
   unsigned completed=a.rhythm.bursts_completed,old_moves=a.boss.move_ticks;int32_t old_x=a.boss.x;
   run_laser(&a);calm_ref_laser(&b);
   if(rest_end && a.laser.phase!=GARDEN_LASER_OFF)cross_rest++;
   run_boss(&a);calm_ref_boss(&b);
   if(tick<100U)CHECK(a.boss.move_ticks==100U-tick,"entrance still consumes exactly100ticks");
   if(tick==100U)CHECK(a.boss.move_ticks==0U && a.rhythm.transits_completed==1U,"entrance reaches first charge/wait without skip");
   if(rest_end){CHECK(a.boss.move_ticks==b.boss.move_ticks && a.boss.target_x==(completed%3U?old_x:b.boss.target_x),"actual REST boundary obeys3-group policy with identical timing");if(completed%3U)holds++;else moves++;}
   if(old_moves && completed%3U)CHECK(a.boss.x==old_x,"first/second train transit slot holds the body at its current x");
   anchored(&a);anchored(&b);laser_ticks[a.laser.phase]++;normalized_equal(&a,&b);
   memset(a.enemy_bullets,0,sizeof(a.enemy_bullets));memset(b.enemy_bullets,0,sizeof(b.enemy_bullets));a.tick++;b.tick++;sequence_steps++;
  }
 }
 CHECK(holds>moves && moves>0U,"real sequences exercise held and moving slots");
 CHECK(laser_ticks[GARDEN_LASER_WARN] && laser_ticks[GARDEN_LASER_FIRE] && laser_ticks[GARDEN_LASER_FADE],"real sequences cover complete laser lifecycle");
}
static void laser_crossing(void)
{
 /* Deliberately cross each laser phase over REST, including low-HP tails. */
 for(unsigned phase=GARDEN_LASER_WARN;phase<=GARDEN_LASER_FADE;phase++)for(unsigned completed=1;completed<=3U;completed++){
  GardenGame a,b;setup(&a,91U+phase*17U+completed,2U);a.boss.rhythm_state=GARDEN_BOSS_REST;a.boss.rhythm_ticks=1U;a.boss.attack=2U;a.rhythm.bursts_completed=completed;
  a.laser=(GardenLaser){.x={a.boss.x+GARDEN_BOSS_MUZZLE_LEFT_X_PX*GARDEN_GRAIN,a.boss.x+GARDEN_BOSS_MUZZLE_RIGHT_X_PX*GARDEN_GRAIN},.y=a.boss.y+GARDEN_BOSS_MUZZLE_Y_PX*GARDEN_GRAIN,.phase=(uint8_t)phase,.ticks=5U};b=a;
  for(unsigned t=0;t<100U;t++){
   run_laser(&a);calm_ref_laser(&b);run_boss(&a);calm_ref_boss(&b);anchored(&a);normalized_equal(&a,&b);
   memset(a.enemy_bullets,0,sizeof(a.enemy_bullets));memset(b.enemy_bullets,0,sizeof(b.enemy_bullets));
  }
 }
 /* Exact duration is a property of the unchanged actual laser state machine. */
 GardenGame a;setup(&a,3U,2U);laser_start(&a);unsigned count[4]={0};
 for(unsigned t=0;t<100U && a.laser.phase!=GARDEN_LASER_OFF;t++){count[a.laser.phase]++;run_laser(&a);}
 CHECK(count[GARDEN_LASER_WARN]==40U && count[GARDEN_LASER_FIRE]==40U && count[GARDEN_LASER_FADE]==12U,"laser duration remains40+40+12ticks");
}
static void waits_and_pause(void)
{
 for(unsigned seed=1;seed<=64U;seed++)for(unsigned band=0;band<3U;band++)for(unsigned attack=1;attack<=3U;attack++){
  GardenGame a,b;setup(&a,seed,band);a.boss.attack=(uint8_t)attack;a.rhythm.bursts_completed=2U;boss_schedule_charge(&a,1U);
  /* Force a valid complete-train capacity deferral at the CHARGE edge. */
  a.boss.rhythm_state=GARDEN_BOSS_CHARGE;a.boss.rhythm_ticks=1U;
  for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++){a.enemy_bullets[i].active=1U;}
  b=a;
  unsigned rng=a.rng,burst=a.boss.burst_left,charge=a.boss.charge_ticks,rest=a.boss.rest_ticks,gap=a.boss.burst_gap;
  run_laser(&a);calm_ref_laser(&b);run_boss(&a);calm_ref_boss(&b);CHECK(a.boss.rhythm_state==GARDEN_BOSS_WAIT,"capacity shortage enters WAIT");normalized_equal(&a,&b);
  for(unsigned t=0;t<25U;t++){run_laser(&a);calm_ref_laser(&b);run_boss(&a);calm_ref_boss(&b);CHECK(a.boss.rhythm_state==GARDEN_BOSS_WAIT && a.rng==rng && a.rhythm.bursts_completed==2U,"WAIT neither consumes RNG nor counts a completed train");normalized_equal(&a,&b);}
  memset(a.enemy_bullets,0,sizeof(a.enemy_bullets));memset(b.enemy_bullets,0,sizeof(b.enemy_bullets));run_laser(&a);calm_ref_laser(&b);run_boss(&a);calm_ref_boss(&b);
  CHECK(a.boss.rhythm_state==GARDEN_BOSS_CHARGE && a.boss.burst_left==burst && a.boss.charge_ticks==charge && a.boss.rest_ticks==rest && a.boss.burst_gap==gap && a.rng==rng,"capacity release resumes announced train and retained timings without reroll");normalized_equal(&a,&b);wait_cases++;
 }
 for(unsigned phase=GARDEN_LASER_WARN;phase<=GARDEN_LASER_FADE;phase++){
  GardenGame a,b;setup(&a,991U+phase,2U);a.paused=1U;a.laser.phase=(uint8_t)phase;a.laser.ticks=5U;a.boss.move_ticks=20U;b=a;
  for(unsigned t=0;t<8U;t++){garden_step(&a,0);calm_ref_step(&b,0);CHECK(a.laser.ticks==5U && a.boss.move_ticks==20U && a.tick==b.tick,"paused calls do not advance laser or movement clocks");CHECK(!memcmp(&a,&b,sizeof(a)),"pause behavior remains byte-identical");}
  garden_step(&a,GARDEN_INPUT_PAUSE);calm_ref_step(&b,GARDEN_INPUT_PAUSE);CHECK(!memcmp(&a,&b,sizeof(a)),"unpause behavior remains byte-identical");
 }
}
static void unchanged_generators_and_waves(void)
{
 for(unsigned seed=1;seed<=64U;seed++)for(unsigned attack=1;attack<=3U;attack++)for(unsigned status=0;status<4U;status++){
  GardenGame a,b;setup(&a,seed,0U);a.boss.status=(uint8_t)status;b=a;
  if(attack==1U)boss_attack1(&a);else if(attack==2U)boss_attack2(&a);else boss_attack3(&a);calm_ref_attack(&b,attack);
  CHECK(!memcmp(&a,&b,sizeof(a)),"all Boss generator outputs identical for same entering state/seed");boss_snapshots++;
 }
 for(unsigned seed=1;seed<=16U;seed++)for(unsigned model=0;model<2U;model++){
  GardenGame a,b;garden_init(&a,seed);CHECK(spawn_enemy(&a,model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,220000,-30000),"wave fixture admitted");b=a;
  for(unsigned t=0;t<600U;t++){run_enemies(&a);calm_ref_enemies(&b);CHECK(!memcmp(&a,&b,sizeof(a)),"fixed low-density waves stay byte-identical");memset(a.enemy_bullets,0,sizeof(a.enemy_bullets));memset(b.enemy_bullets,0,sizeof(b.enemy_bullets));wave_steps++;}
 }
}
int main(void)
{
 direct_rest();sequences();laser_crossing();waits_and_pause();unchanged_generators_and_waves();
 printf("CADENCE rest_cases=%u sequence_steps=%u holds=%u moves=%u natural_cross_rest=%u laser_warn=%u laser_fire=%u laser_fade=%u wait_cases=%u\n",rest_cases,sequence_steps,holds,moves,cross_rest,laser_ticks[1],laser_ticks[2],laser_ticks[3],wait_cases);
 printf("PASS boss_calm checks=%u boss_snapshots=%u wave_steps=%u game_bytes=%u\n",checks,boss_snapshots,wave_steps,(unsigned)sizeof(GardenGame));return 0;
}
