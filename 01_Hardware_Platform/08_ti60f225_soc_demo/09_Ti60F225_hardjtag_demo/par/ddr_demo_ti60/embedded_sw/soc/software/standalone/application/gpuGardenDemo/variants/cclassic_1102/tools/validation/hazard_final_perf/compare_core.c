/* SPDX-License-Identifier: GPL-3.0-or-later
 * Dual-TU replay: no mutation of board sources, no hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "garden_core.h"
void reference_init(GardenGame *,uint32_t);
void optimized_init(GardenGame *,uint32_t);
void reference_step(GardenGame *,uint16_t);
void optimized_step(GardenGame *,uint16_t);
unsigned reference_countdown(const GardenGame *);
unsigned optimized_countdown(const GardenGame *);
unsigned long long reference_probe(const GardenGame *,int32_t *,int32_t *);
unsigned long long optimized_probe(const GardenGame *,int32_t *,int32_t *);
static uint32_t rng=0x52916483u;
static unsigned checks;
static GardenGame benchmark_state;
static int benchmark_set;
static unsigned benchmark_score,benchmark_near,benchmark_young;
static uint32_t rand32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
#define CHECK(c,m) do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u:%s\n",(unsigned)__LINE__,m);exit(2);}}while(0)
static void same(const GardenGame *a,const GardenGame *b,unsigned seed,unsigned tick)
{
    checks++;
    if(memcmp(a,b,sizeof(*a))){
        const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b;
        unsigned at=0;while(at<sizeof(*a)&&x[at]==y[at])at++;
        fprintf(stderr,"Game mismatch seed=%u tick=%u byte=%u ref=%u opt=%u mode=%u liveTick=%u\n",
            seed,tick,at,x[at],y[at],a->mode,a->tick);exit(2);
    }
}
static uint64_t state_hash(const GardenGame *g)
{
    const unsigned char*p=(const unsigned char*)g;uint64_t h=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(*g);i++){h^=p[i];h*=UINT64_C(1099511628211);}return h;
}
static void replay(void)
{
    static const uint32_t seeds[]={12345u,1u,2u,3u,7u,42u,0x47617264u,0xdeadbeefu};
    for(unsigned s=0;s<8;s++){
        GardenGame ref,opt;reference_init(&ref,seeds[s]);optimized_init(&opt,seeds[s]);same(&ref,&opt,seeds[s],0);
        unsigned win=0,over=0,restart=0,peak=0,previous_mode=ref.mode;
        for(unsigned step=0;step<16000u;step++){
            unsigned before=ref.tick;reference_step(&ref,0u);optimized_step(&opt,0u);
            same(&ref,&opt,seeds[s],step+1u);
            CHECK(reference_countdown(&ref)==optimized_countdown(&opt),"public boss forecast observer unchanged");
            if(ref.tick<before)restart++;
            if(ref.mode!=previous_mode){if(ref.mode==GARDEN_MODE_VICTORY)win++;if(ref.mode==GARDEN_MODE_GAMEOVER)over++;}
            previous_mode=ref.mode;if(ref.enemy_bullet_count>peak)peak=ref.enemy_bullet_count;
            if(ref.enemy_bullet_count>=40u&&ref.mode==GARDEN_MODE_BOSS){
                unsigned near=0u,young=0u;
                for(unsigned j=0;j<GARDEN_MAX_ENEMY_BULLETS;j++){
                    const GardenEnemyBullet*b=&ref.enemy_bullets[j];
                    int32_t dx=b->x-ref.player.x,dy=b->y-ref.player.y;
                    if(b->active&&dx>=-240000&&dx<=240000&&dy>=-240000&&dy<=240000){
                        near++;if(b->type==GARDEN_BULLET_DART&&b->age<59u)young++;
                    }
                }
                unsigned cost=young*10u+near;
                if(cost>benchmark_score){benchmark_state=ref;benchmark_set=1;benchmark_score=cost;benchmark_near=near;benchmark_young=young;}
            }
        }
        printf("SEED seed=%u steps=16000 wins=%u gameovers=%u restarts=%u peak=%u hash=%016llx\n",
            seeds[s],win,over,restart,peak,(unsigned long long)state_hash(&ref));
    }
    /* Manual/pause/Q/restart interleave to exercise public interfaces and
     * the exact handover back into automatic pilot. */
    GardenGame ref,opt;reference_init(&ref,12345u);optimized_init(&opt,12345u);
    for(unsigned step=0;step<12000u;step++){
        uint16_t buttons=0u;
        if(step%700u==180u||step%700u==184u)buttons|=GARDEN_INPUT_PAUSE;
        if(step%500u>=450u&&step%500u<470u)buttons|=GARDEN_INPUT_RIGHT|((step&1u)?GARDEN_INPUT_SLOW:0u);
        if(step%500u==470u)buttons|=GARDEN_INPUT_TOGGLE_DEMO;
        if(step%811u==0u)buttons|=GARDEN_INPUT_BOMB;
        if(step%4096u==4095u)buttons|=GARDEN_INPUT_RESTART;
        reference_step(&ref,buttons);optimized_step(&opt,buttons);same(&ref,&opt,12345u,step+1u);
    }
    printf("MIXED steps=12000 hash=%016llx\n",(unsigned long long)state_hash(&ref));
}
static void fixture(GardenGame *g,unsigned test)
{
    reference_init(g,rand32());g->player.x=5000+(int32_t)(rand32()%630001u);
    g->player.y=5000+(int32_t)(rand32()%470001u);
    g->laser.phase=(uint8_t)(rand32()%4u);g->laser.ticks=(uint16_t)(rand32()%46u);
    g->laser.y=(int32_t)(rand32()%480001u);g->laser.x[0]=(int32_t)(rand32()%640001u);g->laser.x[1]=(int32_t)(rand32()%640001u);
    g->boss.active=(uint8_t)(rand32()&1u);g->boss.x=(int32_t)(rand32()%640001u);g->boss.y=(int32_t)(rand32()%480001u);
    for(unsigned i=0;i<GARDEN_MAX_ENEMIES;i++){
        GardenEnemy *e=&g->enemies[i];e->active=(uint8_t)(rand32()&1u);e->type=(uint8_t)(1u+rand32()%2u);
        e->x=(int32_t)(rand32()%840001u)-100000;e->y=(int32_t)(rand32()%680001u)-100000;
        e->vx=(int32_t)(rand32()%24001u)-12000;e->vy=(int32_t)(rand32()%24001u)-12000;
    }
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++){
        GardenEnemyBullet*b=&g->enemy_bullets[i];b->active=(uint8_t)(test%4u==0u?1u:!!(rand32()%3u));
        b->type=(uint8_t)(1u+rand32()%2u);b->angle=(uint16_t)rand32();
        b->x=g->player.x+(int32_t)(rand32()%600001u)-300000;
        b->y=g->player.y+(int32_t)(rand32()%600001u)-300000;
        if(i<8u){static const int32_t edges[]={-240001,-240000,-1,0,1,239999,240000,240001};b->x=g->player.x+edges[i];}
        b->vx=(int32_t)(rand32()%40001u)-20000;b->vy=(int32_t)(rand32()%40001u)-20000;
        b->age=(uint16_t)(rand32()%82u);if(i<8u){static const uint16_t ages[]={0,1,4,5,6,58,59,60};b->age=ages[i];}
        b->ttl=(uint16_t)(rand32()%500u);b->accel=(int16_t)rand32();
    }
}
static void fuzz(void)
{
    for(unsigned test=0;test<6000u;test++){
        GardenGame g,before;fixture(&g,test);before=g;
        int32_t x=(int32_t)(rand32()%24001u)-12000,y=(int32_t)(rand32()%24001u)-12000,ox=x,oy=y;
        if(test<9u){x=ox=0;y=oy=0;memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));g.boss.active=0;
            memset(g.enemies,0,sizeof(g.enemies));g.laser.phase=0;g.player.x=320000;g.player.y=240000;before=g;}
        reference_probe(&g,&x,&y);optimized_probe(&g,&ox,&oy);
        CHECK(x==ox&&y==oy,"same nine-action decision for fuzzed bullets, forming/accel boundaries, edges, actors, lasers and ties");
        CHECK(!memcmp(&g,&before,sizeof(g)),"probe leaves complete Game read-only");
    }
    puts("FUZZ scenes=6000 decision_and_readonly=PASS");
}
static void bench(const GardenGame *g,const char*name,unsigned repetitions)
{
    int32_t x=0,y=0;volatile int64_t checksum=0;
    clock_t start=clock();
    for(unsigned k=0;k<repetitions;k++){x=1200;y=-1000;reference_probe(g,&x,&y);checksum+=x+y;}
    clock_t middle=clock();
    for(unsigned k=0;k<repetitions;k++){x=1200;y=-1000;optimized_probe(g,&x,&y);checksum+=x+y;}
    clock_t end=clock();
    double reference_ms=(double)(middle-start)*1000./CLOCKS_PER_SEC;
    double optimized_ms=(double)(end-middle)*1000./CLOCKS_PER_SEC;
    unsigned long long rc=reference_probe(g,&x,&y);x=1200;y=-1000;unsigned long long oc=optimized_probe(g,&x,&y);
    printf("BENCH name=%s repeats=%u reference_ms=%.3f optimized_ms=%.3f ratio=%.6f ref_sine=%llu opt_sine=%llu checksum=%lld\n",
        name,repetitions,reference_ms,optimized_ms,optimized_ms>0?reference_ms/optimized_ms:0.,rc,oc,(long long)checksum);
}
int main(void)
{
    replay();fuzz();CHECK(benchmark_set,"real dense game snapshot captured");
    printf("SNAPSHOT near=%u young=%u total=%u tick=%u\n",benchmark_near,benchmark_young,benchmark_state.enemy_bullet_count,benchmark_state.tick);
    bench(&benchmark_state,"real_dense",10000u);
    GardenGame dense;reference_init(&dense,12345u);
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++){
        GardenEnemyBullet*b=&dense.enemy_bullets[i];*b=(GardenEnemyBullet){
            .active=1u,.type=GARDEN_BULLET_DART,.age=6u,.ttl=500u,.angle=(uint16_t)(i*29u),
            .accel=(int16_t)(150+i%80u),.x=dense.player.x-80000+(int32_t)(i%16u)*10000,
            .y=dense.player.y-70000+(int32_t)(i/16u)*10000,.vx=(int32_t)(i%7u)*300-900,.vy=1500};
    }
    bench(&dense,"192_young_darts",3000u);
    printf("PASS checks=%u replay_steps=140000 fuzz_scenes=6000 game_bytes=%u\n",checks,(unsigned)sizeof(GardenGame));return 0;
}

