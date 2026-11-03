/* Host-only: actual production core and actual precision asset mapper. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
#include "../src/garden_assets.h"
void fixed_ref_init(GardenGame *,uint32_t);
void fixed_ref_boss_attack(GardenGame *,unsigned);
static unsigned checks,direct_events,direct_bullets,closed_windows,reentries,cycle_shots,cycle_bullets,boss_snapshots;
#define CHECK(c,m)do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);}}while(0)
static unsigned bullet_count(const GardenGame *g)
{unsigned n=0;for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)n+=g->enemy_bullets[i].active!=0;return n;}
/* Decode only the descriptor family/bank; orientation remains independent. */
static void asset_bank(unsigned id,unsigned *type,unsigned *colour)
{
 if(id>=GARDEN_ASSET_DART_BASE && id<GARDEN_ASSET_DIAMOND_BASE){*type=GARDEN_BULLET_DART;*colour=1U+(id-GARDEN_ASSET_DART_BASE)/8U;}
 else if(id>=GARDEN_ASSET_DIAMOND_BASE && id<GARDEN_ASSET_FONT_WHITE_BASE){*type=GARDEN_BULLET_DIAMOND;*colour=1U+(id-GARDEN_ASSET_DIAMOND_BASE)/8U;}
 else{CHECK(id>=GARDEN_ASSET_PRECISION_BASE && id<GARDEN_ASSET_ORDNANCE_HIT_BASE,"precision ID is an enemy-projectile descriptor");unsigned bank=(id-GARDEN_ASSET_PRECISION_BASE)/56U;*type=bank<5U?GARDEN_BULLET_DART:GARDEN_BULLET_DIAMOND;*colour=1U+bank%5U;}
}
static void bullet_contract(const GardenEnemyBullet *b,unsigned model)
{
 unsigned type=model?GARDEN_BULLET_DIAMOND:GARDEN_BULLET_DART,colour=model?4U:1U;
 CHECK(b->active && b->type==type && b->colour==colour,"same model always retains fixed physical type and colour");
 CHECK(b->ttl==500U && b->cord_wait==GARDEN_BULLET_ARM_TICKS && b->age==0,"existing lifetime and forming rules retained");
 unsigned id=garden_bullet_precision_id(b->type,b->colour,b->angle),mapped_type,mapped_colour;asset_bank(id,&mapped_type,&mapped_colour);
 CHECK(mapped_type==type && mapped_colour==colour,"actual precision mapper does not substitute another style or colour bank");
 const garden_sprite_t *sprite=garden_asset(id);
 CHECK(sprite && sprite->width && sprite->height && sprite->offset+(unsigned)sprite->width*sprite->height*2U<=GARDEN_ATLAS_BYTES,"actual sprite descriptor stays inside existing atlas");
 CHECK(garden_asset_is_enemy_bullet(id),"mapped asset is an enemy bullet");
}
static void all_headings(void)
{
 for(unsigned model=0;model<2U;model++){
  unsigned seen[GARDEN_ASSET_COUNT]={0},unique=0;
  for(unsigned angle=0;angle<1024U;angle++){
   unsigned id=garden_bullet_precision_id(model?GARDEN_BULLET_DIAMOND:GARDEN_BULLET_DART,model?4U:1U,angle),type,colour;
   asset_bank(id,&type,&colour);CHECK(type==(model?GARDEN_BULLET_DIAMOND:GARDEN_BULLET_DART) && colour==(model?4U:1U),"all1024 headings retain one design/colour per enemy model");
   CHECK(id<GARDEN_ASSET_COUNT,"heading asset in range");if(!seen[id]){seen[id]=1;unique++;}
  }
  CHECK(unique==64U,"only the expected64 orientation descriptors are selected");
 }
}
static void direct_contracts(void)
{
 for(unsigned seed=1;seed<=1024U;seed++)for(unsigned model=0;model<2U;model++){
  GardenGame g;garden_init(&g,seed);
  for(unsigned entity=0;entity<4U;entity++)g.enemies[entity]=(GardenEnemy){.active=1,.type=model?GARDEN_ENEMY_STOPPER:GARDEN_ENEMY_DARTER,.x=180000+(int)entity*80000,.y=100000+(int)entity*30000,.hp=model?3800:300};
  /* Persistent RNG advances over successive shots; no reset between entities/events. */
  for(unsigned shot=0;shot<8U;shot++)for(unsigned entity=0;entity<4U;entity++){
   GardenEnemy *e=&g.enemies[entity];e->aim_angle=(uint16_t)((seed*37U+entity*193U+shot*71U)&1023U);
   memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));enemy_fire(&g,e);
   unsigned count=bullet_count(&g);CHECK(count==(model?8U:3U),"each emission has fixed model-specific count across seeds/entities/repeated shots");
   CHECK(e->recycle>=(model?120U:36U) && e->recycle<=(model?160U:60U),"large low-density cooldown and small cooldown remain bounded");
   for(unsigned i=0;i<count;i++){
    GardenEnemyBullet *b=&g.enemy_bullets[i];bullet_contract(b,model);
    if(model){CHECK(b->cord_angle==((g.enemy_bullets[0].cord_angle+i*128U)&1023U) && b->spin==0,"large pattern remains one evenly spaced eight-spoke ring");CHECK(b->vx==xpart(b->angle,1700) && b->vy==ypart(b->angle,1700),"large ring retains one1700 speed class");}
    else{CHECK(b->angle==((e->aim_angle+((int)i-1)*32)&1023U),"small pattern remains symmetric32-unit aimed three-shot fan");CHECK(b->accel==70,"small fan retains one70 acceleration class");}
   }
   CHECK(g.dropped_enemy_bullets==0,"direct event not censored by exhausted pool");direct_events++;direct_bullets+=count;
  }
 }
}
static void real_cycles(void)
{
 for(unsigned seed=1;seed<=32U;seed++){
  GardenGame g;garden_init(&g,seed);
  for(unsigned e=0;e<2U;e++)CHECK(spawn_enemy(&g,GARDEN_ENEMY_STOPPER,220000+(int)e*200000,-30000),"two large entities admitted");
  unsigned open[2]={0},emitted[2]={0},entered[2]={0};
  for(unsigned tick=1;tick<=3400U;tick++){
   run_enemies(&g);
   for(unsigned e=0;e<2U;e++){
    GardenEnemy *enemy=&g.enemies[e];CHECK(enemy->active && enemy->hp==3800,"undefeated large enemies retain identity/HP through re-entry");
    if(enemy->phase==2U && !open[e]){open[e]=1;emitted[e]=0;entered[e]=tick;if(enemy->age>200U)reentries++;CHECK(enemy->recycle>=40U && enemy->recycle<=64U,"initial and re-entered phase2 delay remains40..64");}
    if(enemy->phase!=2U && open[e]){CHECK(tick-entered[e]==150U,"actual phase2 window remains150ticks");CHECK(emitted[e]==1U,"every complete large attack window emits exactly one low-density volley");closed_windows++;open[e]=0;}
    unsigned count=0;
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++)if(g.enemy_bullets[i].active && abs32(g.enemy_bullets[i].x-enemy->x)<=25000 && abs32(g.enemy_bullets[i].y-enemy->y)<=25000){count++;bullet_contract(&g.enemy_bullets[i],1);}
    if(count){CHECK(open[e] && count==8U,"real loop emits one complete fixed eight-shot ring only in attack phase");if(!emitted[e])CHECK(tick-entered[e]>=40U && tick-entered[e]<=64U,"actual first shot waits40..64ticks");emitted[e]++;cycle_shots++;cycle_bullets+=count;}
   }
   /* Drain test bullets to measure production, not occupancy/pool drops. */
   memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));g.tick++;CHECK(g.dropped_enemy_bullets==0,"real schedule remains free from allocation censorship");
  }
 }
 CHECK(closed_windows>=800U && reentries>0U,"multi-cycle fixture includes persistent-target repeated attacks");
}
static void boss_unchanged(void)
{
 for(unsigned seed=1;seed<=64U;seed++)for(unsigned attack=1;attack<=3U;attack++)for(unsigned status=0;status<4U;status++){
  GardenGame a,b;garden_init(&a,seed);fixed_ref_init(&b,seed);CHECK(!memcmp(&a,&b,sizeof(a)),"initial state unchanged from low-density parent");
  a.mode=GARDEN_MODE_BOSS;a.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.status=(uint8_t)status,.volley_colour=3};b=a;
  if(attack==1U)boss_attack1(&a);else if(attack==2U)boss_attack2(&a);else boss_attack3(&a);fixed_ref_boss_attack(&b,attack);
  CHECK(!memcmp(&a,&b,sizeof(a)),"Boss generator and all output/RNG state identical for same entering state/seed");boss_snapshots++;
 }
}
int main(void)
{
 all_headings();direct_contracts();real_cycles();boss_unchanged();
 printf("FIXED events=%u bullets=%u seeds=1024 models=2 entities=4 shots=8 headings=1024 orientations=64\n",direct_events,direct_bullets);
 printf("CYCLES seeds=32 ticks=3400 targets=2 closed=%u shots=%u projectiles=%u reentries=%u occupancy_measured=0\n",closed_windows,cycle_shots,cycle_bullets,reentries);
 printf("PASS wave_fixed checks=%u boss_snapshots=%u game_bytes=%u\n",checks,boss_snapshots,(unsigned)sizeof(GardenGame));return 0;
}
