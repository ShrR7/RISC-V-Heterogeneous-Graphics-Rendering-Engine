/* Host-only focused wave balance contracts; no board/renderer/driver mocks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
void wave_ref_init(GardenGame *,uint32_t);
void wave_ref_enemy_fire(GardenGame *,GardenEnemy *);
void wave_ref_run_enemies(GardenGame *);
void wave_ref_boss_attack(GardenGame *,unsigned);
static unsigned checks,styles[3],direct_new,direct_old,closed_new,closed_old,shots_new,shots_old,loops_new,loops_old;
static uint64_t total_new,total_old;
#define CHECK(c,m)do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static unsigned live_bullets(const GardenGame *g)
{unsigned n=0;for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)n+=g->enemy_bullets[i].active!=0;return n;}
static void pair_init(GardenGame *a,GardenGame *b,unsigned seed)
{garden_init(a,seed);wave_ref_init(b,seed);CHECK(!memcmp(a,b,sizeof(*a)),"initial core state unchanged");}
static void direct_contracts(void)
{
 const unsigned counts[]={9,8,6},old_counts[]={18,12,15};
 for(unsigned seed=1;seed<=4096U;seed++){
  GardenGame a,b,p;pair_init(&a,&b,seed);p=a;unsigned style=(unsigned)grand(&p,3U);styles[style]++;
  a.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_STOPPER,.x=320000,.y=160000,.aim_angle=256,.hp=3800};b.enemies[0]=a.enemies[0];
  enemy_fire(&a,&a.enemies[0]);wave_ref_enemy_fire(&b,&b.enemies[0]);
  CHECK(live_bullets(&a)==counts[style] && live_bullets(&b)==old_counts[style],"all three STOPPER styles have exact new/old counts");
  CHECK(a.enemies[0].recycle>=120U && a.enemies[0].recycle<=160U,"new inter-shot cooldown120..160ticks");
  CHECK(b.enemies[0].recycle>=80U && b.enemies[0].recycle<=112U,"frozen parent cooldown80..112ticks");
  CHECK(a.rng==b.rng,"one direct event preserves RNG draw count");
  for(unsigned i=0;i<counts[style];i++){
   const GardenEnemyBullet *eb=&a.enemy_bullets[i];
   CHECK(eb->colour==a.enemy_bullets[0].colour && eb->type==a.enemy_bullets[0].type,"one emission retains one physical style/color");
   CHECK(eb->ttl==500U && eb->cord_wait==GARDEN_BULLET_ARM_TICKS && eb->age==0,"original lifetime and six-tick forming retained");
   if(style==0U)CHECK(!memcmp(eb,&b.enemy_bullets[i],sizeof(*eb)),"three spiral sets retain original first9 trajectories/acceleration");
   else if(style==1U){unsigned expected=(a.enemy_bullets[0].cord_angle+i*128U)&1023U;CHECK(eb->cord_angle==expected && eb->spin==0,"eight ring spokes evenly cover1024angles with no rotation");}
   else {unsigned lane=i/2U,speed=i%2U;CHECK(eb->angle==((256+(int)lane*48-48)&1023) && eb->accel==60+(int)speed*20,"aimed fan has three48-angle lanes and two original accelerations");}
  }
  direct_new+=counts[style];direct_old+=old_counts[style];
  pair_init(&a,&b,seed);a.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=320000,.y=160000,.aim_angle=256,.hp=300};b.enemies[0]=a.enemies[0];
  enemy_fire(&a,&a.enemies[0]);wave_ref_enemy_fire(&b,&b.enemies[0]);
  CHECK(!memcmp(&a,&b,sizeof(a)),"small-enemy direct event/state/RNG remain byte-identical");
 }
 CHECK(styles[0] && styles[1] && styles[2],"4096seeds cover all large/small firing styles");
}
typedef struct{unsigned closed,shots,reentries,total,first_delay_min,first_delay_max;} WindowStats;
static WindowStats cycles(unsigned seed,unsigned reference)
{
 GardenGame g;garden_init(&g,seed);WindowStats out={.first_delay_min=1000U};
 for(unsigned e=0;e<2U;e++){CHECK(spawn_enemy(&g,GARDEN_ENEMY_STOPPER,220000+(int)e*200000,-30000),"two persistent large targets admitted");}
 unsigned open[2]={0},emitted[2]={0},entered[2]={0};
 for(unsigned tick=1;tick<=3400U;tick++){
  if(reference)wave_ref_run_enemies(&g);else run_enemies(&g);
  for(unsigned e=0;e<2U;e++){
   GardenEnemy *enemy=&g.enemies[e];
   CHECK(enemy->active && enemy->hp==3800,"undefeated large targets retain identity/HP through waits and age re-entry");
   if(enemy->phase==2U && !open[e]){
    open[e]=1;emitted[e]=0;entered[e]=tick;if(enemy->age>200U)out.reentries++;
    CHECK(enemy->recycle>=(reference?12U:40U) && enemy->recycle<=(reference?32U:64U),"every initial/re-entered attack window has expected first delay");
   }
   if(enemy->phase!=2U && open[e]){
    CHECK(tick-entered[e]==150U,"real run_enemies closes exactly150tick attack window");
    CHECK(emitted[e]==(reference?2U:1U),"each complete large attack window emits exactly parent2/new1 waves");
    out.closed++;open[e]=0;
   }
   unsigned count=0;
   for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)if(g.enemy_bullets[i].active &&
       abs32(g.enemy_bullets[i].x-enemy->x)<=25000 && abs32(g.enemy_bullets[i].y-enemy->y)<=25000)count++;
   if(count){
    CHECK(open[e],"large emission only occurs in an attack window");
    if(!emitted[e]){unsigned delay=tick-entered[e];CHECK(delay>=(reference?12U:40U) && delay<=(reference?32U:64U),"actual first emission waits expected number of ticks");if(delay<out.first_delay_min)out.first_delay_min=delay;if(delay>out.first_delay_max)out.first_delay_max=delay;}
    CHECK(reference?(count==18U||count==12U||count==15U):(count==9U||count==8U||count==6U),"real-loop emissions have complete finite volley counts");
    emitted[e]++;out.shots++;out.total+=count;
   }
  }
  /* Allocation-neutral schedule fixture: drain old projectiles each step so
   * full-pool drops cannot censor the measured number of emitted bullets.
   * This measures cumulative production, not on-screen occupancy/FPS. */
  memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));g.tick++;
  CHECK(g.dropped_enemy_bullets==0,"schedule result not hidden by pool exhaustion");
 }
 return out;
}
static void cycle_contracts(void)
{
 for(unsigned seed=1;seed<=32U;seed++){
  WindowStats a=cycles(seed,0),b=cycles(seed,1);
  CHECK(a.closed==b.closed && a.closed>=20U && a.reentries>0U,"multi-cycle sample includes repeated attack windows and persistent targets");
  CHECK(a.total*100U<b.total*40U,"actual-loop cumulative large-enemy projectiles fall by more than60percent");
  total_new+=a.total;total_old+=b.total;closed_new+=a.closed;closed_old+=b.closed;shots_new+=a.shots;shots_old+=b.shots;loops_new+=a.reentries;loops_old+=b.reentries;
 }
}
static void unchanged_small_and_boss(void)
{
 for(unsigned seed=1;seed<=64U;seed++){
  GardenGame a,b;pair_init(&a,&b,seed);CHECK(spawn_enemy(&a,GARDEN_ENEMY_DARTER,220000,-30000),"small fixture admitted");b=a;
  for(unsigned tick=0;tick<600U;tick++){
   run_enemies(&a);wave_ref_run_enemies(&b);CHECK(!memcmp(&a,&b,sizeof(a)),"real small-enemy movement/firing/RNG state byte-identical across cycles");
   memset(a.enemy_bullets,0,sizeof(a.enemy_bullets));memset(b.enemy_bullets,0,sizeof(b.enemy_bullets));
  }
  for(unsigned attack=1;attack<=3;attack++)for(unsigned status=0;status<4;status++){
   pair_init(&a,&b,seed);a.mode=GARDEN_MODE_BOSS;a.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.status=(uint8_t)status,.volley_colour=3};b=a;
   if(attack==1U)boss_attack1(&a);else if(attack==2U)boss_attack2(&a);else boss_attack3(&a);
   wave_ref_boss_attack(&b,attack);CHECK(!memcmp(&a,&b,sizeof(a)),"three Boss generators/all status branches preserve complete seeded state");
  }
 }
}
int main(void)
{
 direct_contracts();cycle_contracts();unchanged_small_and_boss();
 printf("STYLES counts=%u,%u,%u direct_projectiles_new=%u old=%u\n",styles[0],styles[1],styles[2],direct_new,direct_old);
 printf("CYCLES seeds=32 ticks=3400 targets=2 closed_new=%u closed_old=%u shots_new=%u shots_old=%u projectiles_new=%llu projectiles_old=%llu reentries_new=%u reentries_old=%u occupancy_measured=0\n",closed_new,closed_old,shots_new,shots_old,(unsigned long long)total_new,(unsigned long long)total_old,loops_new,loops_old);
 printf("PASS wave_density checks=%u direct_seeds=4096 cycle_seeds=32 small_steps=38400 boss_snapshots=768 game_bytes=%u\n",checks,(unsigned)sizeof(GardenGame));return 0;
}
