/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent host atan2 oracle. Float/division here are test-only.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "garden_heading_precision.h"
#include "garden_fx.h"
#include "garden_assets.h"
#include <string.h>
static uint64_t checks,vectors;
static double worst[2];static unsigned near_boundary[2];
#define CHECK(c,m) do { checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m);exit(2);} } while(0)
static uint32_t random_state=0xE1F60C32U;
static uint32_t random32(void){uint32_t x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x;}
static int32_t signed32(uint32_t x){return x<=INT32_MAX ? (int32_t)x:-1-(int32_t)(UINT32_MAX-x);}
/* Frozen pre-optimization linear Q30 oracle, deliberately independent of
 * the production binary-search/predicate paths. */
static uint16_t original_linear(unsigned which,int32_t vx,int32_t vy,uint32_t fallback)
{
    static const uint32_t t32[4]={105754339U,325716021U,573926962U,881197142U};
    static const uint32_t t64[8]={52749553U,159274554U,268958326U,384190968U,507842061U,643576082U,796341210U,973182862U};
    unsigned count=which ? 8U:4U,step=which ? 16U:32U;
    const uint32_t *threshold=which ? t64:t32;
    uint32_t ax=vx<0 ? 0U-(uint32_t)vx:(uint32_t)vx,ay=vy<0 ? 0U-(uint32_t)vy:(uint32_t)vy;
    if(!(ax|ay)) return (uint16_t)(((fallback+(step>>1))&1023U)&~(step-1U));
    uint32_t major=ax<ay ? ay:ax,minor=ax<ay ? ax:ay;unsigned bin=0U;
    while(bin<count && ((uint64_t)minor<<30)>=(uint64_t)major*threshold[bin]) bin++;
    unsigned result=bin*step;if(ay>ax) result=256U-result;if(vx<0) result=512U-result;if(vy<0) result=(1024U-result)&1023U;
    return (uint16_t)result;
}
static void candidates(int32_t x,int32_t y,uint32_t fallback)
{
    unsigned expected=original_linear(1U,x,y,fallback),accepted=0U;
    for(unsigned candidate=0;candidate<1024U;candidate+=16U) {
        unsigned actual=heading64_matches(x,y,fallback,candidate);
        CHECK(actual==(candidate==expected),"every candidate predicate agrees with frozen original linear algorithm");accepted+=actual;
    }
    CHECK(accepted==1U && !heading64_matches(x,y,fallback,1U) && !heading64_matches(x,y,fallback,1024U) &&
          !heading64_matches(x,y,fallback,UINT_MAX),"unique accepted exact grid angle; malformed candidate never accepted");
}
static uint16_t call(unsigned which,int32_t x,int32_t y,uint32_t fallback)
{return which ? heading64_from_velocity(x,y,fallback):heading32_from_velocity(x,y,fallback);}
static void vector(int32_t x,int32_t y)
{
    if(!x && !y) return;
    const double pi=acos(-1.0),epsilon=0.5/1073741824.0*180.0/pi;
    double angle=atan2((double)y,(double)x);if(angle<0.0) angle+=2.0*pi;
    for(unsigned which=0;which<2U;which++) {
        unsigned directions=which ? 64U:32U,step=1024U/directions;
        uint16_t result=call(which,x,y,0xFFFFFFFFU);
        CHECK(result==original_linear(which,x,y,0xFFFFFFFFU),"production binary search is bit-exact to old linear scan across all tested vectors");
        if(which) CHECK(heading64_matches(x,y,0xFFFFFFFFU,result) &&
            !heading64_matches(x,y,0xFFFFFFFFU,(result+16U)&1023U) &&
            !heading64_matches(x,y,0xFFFFFFFFU,(result+1008U)&1023U) &&
            !heading64_matches(x,y,0xFFFFFFFFU,result^512U),"exact bounds accept real bin and reject both adjacent/opposite bins");
        CHECK(result<=1024U-step && result%step==0U,"result is a bounded exact1024-turn32/64 grid angle");
        double error=fabs(angle*180.0/pi-result*360.0/1024.0);if(error>180.0) error=360.0-error;
        if(error>worst[which]) worst[which]=error;
        if(error>180.0/directions+epsilon+1e-11) {
            fprintf(stderr,"vector x=%d y=%d n=%u result=%u error=%.12f\n",x,y,directions,result,error);
            CHECK(0,"atan2 angular error exceeds nominal half-bin plus declared Q30 boundary epsilon");
        }
        CHECK(error<=180.0/directions+epsilon+1e-11,"independent atan2 angular bound");
        unsigned expected=((unsigned)floor(angle*directions/(2.0*pi)+0.5))&(directions-1U);
        if(result!=expected*step) {
            near_boundary[which]++;
            CHECK(fabs(error-180.0/directions)<=epsilon+1e-11,"any oracle-bin disagreement is confined to declared finite-Q30 boundary epsilon");
        }
    }
    vectors++;
}
static void octants(int64_t major,int64_t minor)
{
    int64_t xs[8]={major,minor,-minor,-major,-major,-minor,minor,major};
    int64_t ys[8]={minor,major,major,minor,-minor,-major,-major,-minor};
    for(unsigned i=0;i<8U;i++) if(xs[i]>=INT32_MIN && xs[i]<=INT32_MAX && ys[i]>=INT32_MIN && ys[i]<=INT32_MAX)
        vector((int32_t)xs[i],(int32_t)ys[i]);
}
static void math_tests(void)
{
    const double pi=acos(-1.0);
    static const uint32_t boundaries[]={52749553U,159274554U,268958326U,384190968U,507842061U,643576082U,796341210U,973182862U};
    /* major2^30 makes every Q30 cross-product boundary an EXACT integer
     * equality. Exercise equality and both nearest integer neighbors in8octants. */
    for(unsigned k=0;k<8U;k++) for(int delta=-1;delta<=1;delta++) {
        int32_t major=1073741824,minor=(int32_t)boundaries[k]+delta;
        int32_t xs[8]={major,minor,-minor,-major,-major,-minor,minor,major};
        int32_t ys[8]={minor,major,major,minor,-minor,-major,-major,-minor};
        for(unsigned o=0;o<8;o++) {vector(xs[o],ys[o]);candidates(xs[o],ys[o],UINT_MAX);}
    }
    for(int32_t x=-16;x<=16;x++) for(int32_t y=-16;y<=16;y++) candidates(x,y,UINT_MAX);
    for(unsigned fallback=0;fallback<1024;fallback++) candidates(0,0,fallback|0xFFFF0000U);
    const int32_t extremes[]={INT32_MIN,INT32_MIN+1,-1073741824,-1000000,-1,0,1,1000000,1073741824,INT32_MAX-1,INT32_MAX};
    for(unsigned i=0;i<sizeof(extremes)/sizeof(extremes[0]);i++)
    for(unsigned j=0;j<sizeof(extremes)/sizeof(extremes[0]);j++) vector(extremes[i],extremes[j]);
    CHECK(heading32_from_velocity(INT32_MAX,0,0)==0 && heading32_from_velocity(0,INT32_MAX,0)==256 &&
          heading32_from_velocity(INT32_MIN,0,0)==512 && heading32_from_velocity(0,INT32_MIN,0)==768,"axes use core east/south/west/north convention");
    for(unsigned which=0;which<2U;which++) {
        unsigned n=which ? 64U:32U,step=1024U/n;
        for(unsigned ray=0;ray<n;ray++) {
            double a=2.0*pi*ray/n;
            int32_t x=(int32_t)llround(cos(a)*1000000000.0),y=(int32_t)llround(sin(a)*1000000000.0);
            CHECK(call(which,x,y,0U)==ray*step,"every literal32/64 ray quantizes to its own exact core angle");vector(x,y);
        }
        const int64_t majors[]={1,2,3,31,1000,1048576,1073741824,2147483647,2147483648LL};
        for(unsigned k=0;k<n/8U;k++) {
            double tan_boundary=tan((2.0*k+1.0)*pi/n);
            for(unsigned j=0;j<sizeof(majors)/sizeof(majors[0]);j++) {
                int64_t centre=llround(tan_boundary*majors[j]);
                for(int delta=-2;delta<=2;delta++) if(centre+delta>=0) octants(majors[j],centre+delta);
            }
        }
    }
    for(int x=-256;x<=256;x++) for(int y=-256;y<=256;y++) vector(x,y);
    for(unsigned i=0;i<2000000U;i++) {int32_t x=signed32(random32()),y=signed32(random32());vector(x,y);}
    const uint32_t bases[]={0U,0x80000000U,0xFFFF0000U};
    for(unsigned which=0;which<2U;which++) {
        unsigned step=which ? 16U:32U;
        for(unsigned b=0;b<3U;b++) for(unsigned angle=0;angle<65536U;angle++) {
            uint32_t raw=bases[b]+angle;
            unsigned expected=((raw&1023U)+step/2U)/step*step&1023U;
            CHECK(call(which,0,0,raw)==original_linear(which,0,0,raw),"stationary binary result exact to original inclusive fallback wrap");
            if(which) CHECK(heading64_matches(0,0,raw,expected),"stationary predicate agrees across all16bit fallback values/high prefixes");
            CHECK(call(which,0,0,raw)==expected,"zero-speed fallback nearest-bin tie and wrapping oracle across all16-bit angles/high prefixes");
        }
    }
    printf("PASS vectors=%llu checks=%llu worst32_deg=%.12f worst64_deg=%.12f Q30_epsilon_deg=%.12g near_boundary32=%u near_boundary64=%u\n",
        (unsigned long long)vectors,(unsigned long long)checks,worst[0],worst[1],0.5/1073741824.0*180.0/pi,near_boundary[0],near_boundary[1]);
}

/* Public asset contract oracle; libm establishes movement directions. */
static unsigned oracle64(int32_t vx,int32_t vy,unsigned fallback)
{
    if(!vx && !vy) return (((fallback&1023U)+8U)/16U%64U)*16U;
    double a=atan2((double)vy,(double)vx);if(a<0.0) a+=2.0*acos(-1.0);
    return ((unsigned)floor(a*64.0/(2.0*acos(-1.0))+0.5)&63U)*16U;
}
static unsigned old_oracle(int32_t vx,int32_t vy,unsigned fallback)
{
    if(!vx && !vy) return (((fallback&1023U)+64U)/128U%8U)*128U;
    double a=atan2((double)vy,(double)vx);if(a<0.0) a+=2.0*acos(-1.0);
    return ((unsigned)floor(a*8.0/(2.0*acos(-1.0))+0.5)&7U)*128U;
}
static unsigned expected_id(unsigned type,unsigned colour,unsigned angle)
{
    unsigned bin=(((angle&1023U)+8U)/16U)&63U;
    if(colour<1U || colour>5U) colour=3U;
    if(!(bin&7U)) return (type==2U ? 52U:12U)+(colour-1U)*8U+bin/8U;
    return 280U+(type==2U ? 280U:0U)+(colour-1U)*56U+bin-bin/8U-1U;
}
static void descriptors_and_ids(void)
{
    unsigned used[840]={0},fine=0,legacy=0;
    CHECK(GARDEN_ASSET_COUNT==840U && GARDEN_ASSET_PRECISION_BASE==280U &&
          GARDEN_PRECISION_ROTATIONS==64U && GARDEN_BULLET_ROTATIONS==8U,
          "append-only precision atlas keeps original eight direction API and IDs");
    for(unsigned type=1U;type<=2U;type++) for(unsigned colour=1U;colour<=5U;colour++)
    for(unsigned bin=0U;bin<64U;bin++) {
        unsigned id=garden_bullet_precision_id(type,colour,bin*16U);
        CHECK(id==expected_id(type,colour,bin*16U) && id<840U && !used[id],
              "each of two types/five colours/64 exact bins maps to unique contracted ID");
        used[id]++;
        CHECK(garden_asset_is_enemy_bullet(id),"precision and reused old IDs recognized as enemy bullets");
        if(!(bin&7U)) {
            CHECK(id==garden_bullet_id(type,colour,bin*16U),"exact original cardinal/diagonal bins reuse old frozen IDs");
            legacy++;
        } else {CHECK(id>=280U,"fine direction lives only in append-only range");fine++;}
    }
    CHECK(legacy==80U && fine==560U,"old80 and appended560 enemy frames are all individually reachable");
    for(unsigned id=0;id<840U;id++) {
        const garden_sprite_t *a=garden_asset(id);
        CHECK(a && a->width && a->height && a->stride==2U*a->width,"every real C descriptor has positive tight RGB565 bounds");
        CHECK(!(a->offset&3U) && a->offset+(a->height-1U)*a->stride+a->width*2U<=GARDEN_ATLAS_BYTES,
              "all C descriptors four-byte aligned and bounded by exact atlas bytes");
        CHECK(a->anchor_x>=0 && a->anchor_y>=0 && a->anchor_x<(int)a->width && a->anchor_y<(int)a->height,
              "rotated atlas anchors remain inside their own physical geometry");
        CHECK(garden_asset_is_enemy_bullet(id)==((id>=12U && id<=91U) || id>=280U),
              "enemy ID classification never consumes font/player/scene IDs");
    }
    CHECK(!garden_asset(840U) && !garden_asset(UINT_MAX) && !garden_asset_is_enemy_bullet(840U) && !garden_asset_is_enemy_bullet(UINT_MAX),
          "invalid descriptors and classification bounded");
    puts("PASS binary_vs_original_linear=1 exact_Q30_ties_all_octants=1 unique64_candidate_bounds=1");
    const unsigned raws[]={0U,7U,8U,15U,16U,1023U,1024U,UINT_MAX,UINT_MAX-8U};
    const unsigned types[]={0U,1U,2U,3U,UINT_MAX},colours[]={0U,1U,2U,3U,4U,5U,6U,UINT_MAX};
    for(unsigned a=0;a<sizeof(raws)/sizeof(*raws);a++) for(unsigned t=0;t<sizeof(types)/sizeof(*types);t++)
    for(unsigned c=0;c<sizeof(colours)/sizeof(*colours);c++)
        CHECK(garden_bullet_precision_id(types[t],colours[c],raws[a])==expected_id(types[t],colours[c],raws[a]),
              "precision angle midpoint/wrap, invalid colour fallback3 and legacy type2 policy");
    for(int32_t x=-64;x<=64;x++) for(int32_t y=-64;y<=64;y++)
        CHECK(garden_fx_bullet_heading(x,y,UINT_MAX)==old_oracle(x,y,UINT_MAX),"legacy8 FX helper retained against independent atan2 oracle");
    puts("PASS ids old=80 fine=560 types=2 colours=5 all_C_descriptors=840 legacy8_retained=1");
}
static GardenGame game,saved;
static uint32_t fixture_draws,natural_draws,real_steps,spinning_verified,precise_natural;
static void pure_build(GardenFx *fx,GardenDraw *d,unsigned capacity,unsigned alpha,unsigned *n)
{
    saved=game;garden_fx_observe(fx,&game);*n=garden_fx_build(fx,&game,d,capacity,alpha);
    CHECK(!memcmp(&saved,&game,sizeof(game)),"actual production FX observer/build preserves complete game bytes");
}
static int same_draw(GardenDraw a,GardenDraw b)
{return a.x==b.x && a.y==b.y && a.asset==b.asset && a.alpha==b.alpha;}
static void fixture_all_directions(void)
{
    const double pi=acos(-1.0);
    GardenFx fx;GardenDraw output[64],first[64];
    for(unsigned type=1U;type<=2U;type++) for(unsigned colour=1U;colour<=5U;colour++) {
        memset(&game,0,sizeof(game));game.mode=GARDEN_MODE_BOSS;
        for(unsigned bin=0;bin<64U;bin++) {
            double a=2.0*pi*bin/64U;
            GardenEnemyBullet *b=&game.enemy_bullets[bin];
            *b=(GardenEnemyBullet){.active=1,.type=(uint8_t)type,.colour=(uint8_t)colour,
                .x=-9876+(int32_t)bin*10123,.y=100876+(int32_t)bin*6001,
                .vx=(int32_t)llround(cos(a)*1000000.0),.vy=(int32_t)llround(sin(a)*1000000.0),
                .angle=(uint16_t)((bin*16U+384U)&1023U),.cord_angle=(uint16_t)((bin*16U+128U)&1023U),
                .spin=-12,.cord_wait=(int16_t)(bin&1U ? -1:20),.ttl=100};
        }
        game.enemy_bullet_count=64;
        garden_fx_reset(&fx);garden_fx_set_flags(&fx,0);
        for(unsigned alpha=0;alpha<2U;alpha++) {
            unsigned n;pure_build(&fx,output,64,alpha,&n);
            CHECK(n==64U && !fx.stats.essential_dropped,"all64 directions admitted using one sprite each in either Alpha mode");
            for(unsigned i=0;i<n;i++) {
                const GardenEnemyBullet *b=&game.enemy_bullets[i];
                CHECK(output[i].asset==expected_id(type,colour,i*16U),"production FX uses actual velocity for every64-direction type/colour frame");
                CHECK(output[i].x==b->x/GARDEN_GRAIN && output[i].y==b->y/GARDEN_GRAIN && output[i].alpha==32U,
                      "position raw division and full opacity unchanged despite spin/activation-cord displacement");
                CHECK(output[i].asset!=expected_id(type,colour,b->angle),"unrelated decorative spin cannot control real sprite direction");
                if(alpha) CHECK(same_draw(output[i],first[i]),"Alpha selection never changes real projectile asset/position/opacity");
                else first[i]=output[i];
                fixture_draws++;
            }
        }
        game.paused=1U;unsigned n;pure_build(&fx,output,64,1,&n);
        CHECK(n==64U,"pause retains all independent projectile draws");
        for(unsigned i=0;i<n;i++) CHECK(same_draw(output[i],first[i]),"same tick and pause stable asset/position/opacity");
    }
    memset(&game,0,sizeof(game));garden_fx_reset(&fx);
    const unsigned fallback[]={0U,7U,8U,119U,120U,503U,504U,1015U,1016U,1023U};
    for(unsigned i=0;i<sizeof(fallback)/sizeof(*fallback);i++) {
        game.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=2,.colour=5,.x=123456,.y=-3456,.angle=256,.cord_angle=(uint16_t)fallback[i]};
        unsigned n;pure_build(&fx,output,1,1,&n);
        CHECK(n==1U && output[0].asset==expected_id(2,5,oracle64(0,0,fallback[i])),"at-rest newborn uses nearest64 stable firing cord angle, not spin");
    }
    puts("PASS production_fixture independent_velocity_bins=640 both_alpha_modes=1 stationary_pause_purity=1");
}
static unsigned prefix_count(const GardenGame *g)
{
    unsigned n=0;
    for(unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) n+=g->enemies[i].active!=0U;
    n+=g->boss.active!=0U;n+=g->player.active && (!g->player.grace || (g->tick&3U)<2U);
    for(unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) n+=g->player_bullets[i].active!=0U;
    return n;
}
static void natural_core(void)
{
    GardenGame baseline,previous;GardenFx fx;GardenDraw draws[GARDEN_DRAW_MAX];
    garden_init(&game,12345U);baseline=game;garden_fx_reset(&fx);garden_fx_set_flags(&fx,0);
    for(unsigned step=0;step<12000U;step++) {
        uint16_t input=0;
        if(step%3500U==1800U || step%3500U==1825U) input|=GARDEN_INPUT_PAUSE;
        if(step%3500U==3300U) input|=GARDEN_INPUT_RESTART;
        previous=game;garden_step(&baseline,input);garden_step(&game,input);
        unsigned n;pure_build(&fx,draws,GARDEN_DRAW_MAX,step&1U,&n);
        CHECK(!memcmp(&baseline,&game,sizeof(game)),"12000 true logic steps including pauses/restarts remain byte-exact vs no-render game");
        unsigned index=prefix_count(&game);
        for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) if(game.enemy_bullets[i].active) {
            const GardenEnemyBullet *b=&game.enemy_bullets[i],*old=&previous.enemy_bullets[i];
            unsigned heading=oracle64(b->vx,b->vy,b->cord_angle);
            CHECK(index<n && draws[index].asset==expected_id(b->type,b->colour,heading),"real production FX heading matches independent64 atan2 at natural game velocities");
            CHECK(draws[index].x==b->x/GARDEN_GRAIN && draws[index].y==b->y/GARDEN_GRAIN && draws[index].alpha==32U,
                  "natural projectile collision anchor/position and opacity never changed");
            if(draws[index].asset>=280U) precise_natural++;
            if(game.tick==previous.tick+1U && old->active && b->type==2U && b->spin && old->age+1U==b->age &&
               b->angle==((old->angle+old->spin)&1023U) && b->vx==old->vx && b->vy==old->vy &&
               b->x==old->x+old->vx && b->y==old->y+old->vy &&
               heading!=(((b->angle+8U)&1023U)&~15U)) spinning_verified++;
            index++;natural_draws++;
        }
        CHECK(fx.stats.essential_dropped==0U,"natural game essential draw budget retained");
        real_steps++;
    }
    CHECK(spinning_verified>100U && precise_natural>1000U && natural_draws>10000U,
          "real Boss spinning Diamonds and fine direction asset use exercised beyond trivial synthetic inputs");
    printf("PASS natural real_steps=%u enemy_draws=%u fine_draws=%u spinning_verified=%u byte_pure=1\n",real_steps,natural_draws,precise_natural,spinning_verified);
}
static void capacity_priority(void)
{
    GardenFx fx;GardenDraw essentials[234];
    struct {uint32_t before[4];GardenDraw d[GARDEN_DRAW_MAX];uint32_t after[4];} guarded;
    memset(&game,0,sizeof(game));game.mode=GARDEN_MODE_BOSS;game.player.active=1;game.player.x=80000;game.player.y=350000;
    for(unsigned i=0;i<8U;i++) game.enemies[i]=(GardenEnemy){.active=1,.type=1,.x=(int32_t)i*17000,.y=120000};
    game.boss=(GardenBoss){.active=1,.x=320000,.y=121000,.flash=3};
    for(unsigned i=0;i<24U;i++) game.player_bullets[i]=(GardenPlayerBullet){.active=1,.x=(int32_t)i*10000,.y=200000};
    for(unsigned i=0;i<192U;i++) game.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=(uint8_t)(1U+i%2U),.colour=(uint8_t)(1U+i%5U),
        .x=(int32_t)i*3100,.y=240000,.vx=1000,.vy=100,.angle=512,.cord_angle=256};
    for(unsigned i=0;i<8U;i++) game.effects[i]=(GardenEffect){.active=1,.big=(uint8_t)(i&1U),.x=(int32_t)i*25000,.y=180000};
    garden_fx_reset(&fx);garden_fx_observe(&fx,&game);
    game.tick++;game.player.x+=12000;garden_fx_observe(&fx,&game);
    game.tick++;game.player.x+=12000;garden_fx_observe(&fx,&game);
    unsigned n;pure_build(&fx,essentials,234,0,&n);
    CHECK(n==234U && essentials[8].asset==GARDEN_ASSET_BOSS1 && fx.stats.flash_sprites==0U,
          "full234 essentials include readable Boss unchanged during hits");
    for(unsigned alpha=0;alpha<2U;alpha++) for(unsigned cap=0;cap<=250U;cap++) {
        memset(&guarded,0xA5,sizeof(guarded));pure_build(&fx,guarded.d,cap,alpha,&n);
        unsigned limit=cap<GARDEN_DRAW_MAX-alpha ? cap:GARDEN_DRAW_MAX-alpha;
        unsigned admitted=limit<234U ? limit:234U;
        unsigned decor=fx.stats.ghosts+fx.stats.rings;
        CHECK(n<=limit && n==admitted+decor,"draw limit and Alpha panel reservation preserved for every capacity0..250");
        CHECK(fx.stats.essential_dropped==234U-admitted,"essential priority/dropped statistics exact under every admission limit");
        for(unsigned i=0;i<admitted;i++) CHECK(same_draw(guarded.d[decor+i],essentials[i]),"all admitted real actors/projectiles/effects preserve order ahead of decoration admission");
        const unsigned char *unused=(const unsigned char *)&guarded.d[n];
        size_t bytes=(GARDEN_DRAW_MAX-n)*sizeof(GardenDraw)+sizeof(guarded.after);
        for(size_t i=0;i<bytes;i++) CHECK(unused[i]==0xA5,"no writes after returned count/caller capacity/end canary");
        for(unsigned i=0;i<4U;i++) CHECK(guarded.before[i]==0xA5A5A5A5U,"leading canary never changed");
        if(limit<=234U) CHECK(!decor,"under pressure all decoration drops before any core essential");
        if(alpha && limit==235U) CHECK(decor==1U && guarded.d[0].alpha==12U && guarded.d[0].x==92,
            "single spare decoration retains brighter newer true-position ghost");
        if(alpha && limit>=238U) CHECK(n==238U && fx.stats.ghosts==2U && fx.stats.rings==2U && fx.stats.essential_dropped==0U,
            "max synthetic pool234 keeps2 ghosts+2rings without losing a projectile");
    }
    memset(&guarded,0xA5,sizeof(guarded));pure_build(&fx,NULL,250,1,&n);
    CHECK(n==0U && fx.stats.essential_dropped==234U,"null draw pointer causes no write and audits omitted essentials");
    CHECK(garden_fx_build(NULL,&game,guarded.d,240,1)==0U && garden_fx_build(&fx,NULL,guarded.d,240,1)==0U,
          "null observer/builder inputs bounded");
    CHECK(((unsigned char *)guarded.d)[0]==0xA5,"null guard paths never touch capacity memory");
    puts("PASS capacity capacities=502 essential=234 decoration=4 panel_reservation=1 canary_null_priority=1");
}
int main(void)
{
    math_tests();descriptors_and_ids();fixture_all_directions();natural_core();capacity_priority();
    printf("SUMMARY checks=%llu math_vectors=%llu real_steps=%u fixture_draws=%u natural_draws=%u precise_natural=%u spinning_verified=%u\n",
        (unsigned long long)checks,(unsigned long long)vectors,real_steps,fixture_draws,natural_draws,precise_natural,spinning_verified);
    return 0;
}
