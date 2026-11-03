/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent libm heading oracle and real production draw-list contracts.
 * Host only: no FPGA/framebuffer/FPS acceptance is claimed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "garden_fx.h"
#include "garden_assets.h"

static unsigned checks;
#define CHECK(c,m) do { checks++;if(!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m);exit(2); } } while(0)
static unsigned oracle(int32_t vx,int32_t vy,unsigned fallback)
{
    if(!vx && !vy) return (((fallback&1023U)+64U)/128U%8U)*128U;
    double angle=atan2((double)vy,(double)vx);
    if(angle<0.0) angle+=8.0*atan(1.0);
    return ((unsigned)floor(angle/atan(1.0)+0.5)%8U)*128U;
}
static void directions(void)
{
    static const struct { int32_t vx,vy;unsigned angle; } literals[]={
        {1,0,0},{1,1,128},{0,1,256},{-1,1,384},
        {-1,0,512},{-1,-1,640},{0,-1,768},{1,-1,896},
        {1000000,414213,0},{1000000,414214,128},
        {414213,1000000,256},{414214,1000000,128},
        {-1000000,414213,512},{-1000000,414214,384},
        {1000000,-414213,0},{1000000,-414214,896},
        {-414213,-1000000,768},{-414214,-1000000,640},
        {INT32_MIN,0,512},{0,INT32_MIN,768},{INT32_MIN,INT32_MIN,640},
        {INT32_MAX,INT32_MIN,896},{INT32_MIN,INT32_MAX,384},
        {INT32_MAX,1,0},{1,INT32_MIN,768}
    };
    for(unsigned i=0;i<sizeof(literals)/sizeof(*literals);i++)
        CHECK(garden_fx_bullet_heading(literals[i].vx,literals[i].vy,256)==literals[i].angle,"literal axes/diagonals/22.5-degree boundaries/extreme signs select expected exact octant");
    static const struct { unsigned fallback,angle; } still[]={
        {0,0},{63,0},{64,128},{191,128},{192,256},{255,256},
        {511,512},{512,512},{895,896},{959,896},{960,0},
        {1023,0},{1024,0},{UINT_MAX,0},{UINT_MAX-64U,896}
    };
    for(unsigned i=0;i<sizeof(still)/sizeof(*still);i++)
        CHECK(garden_fx_bullet_heading(0,0,still[i].fallback)==still[i].angle,"zero velocity uses stable fallback with safe angular wrap, including UINT_MAX");
    for(int32_t vx=-96;vx<=96;vx++) for(int32_t vy=-96;vy<=96;vy++)
        CHECK(garden_fx_bullet_heading(vx,vy,128)==oracle(vx,vy,128),"dense velocity grid agrees with independent atan2/floor nearest-octant oracle");
    static const int32_t extremes[]={INT32_MIN,INT32_MIN+1,-2000000000,-1000000,-1,0,1,1000000,2000000000,INT32_MAX-1,INT32_MAX};
    for(unsigned x=0;x<sizeof(extremes)/sizeof(*extremes);x++)
        for(unsigned y=0;y<sizeof(extremes)/sizeof(*extremes);y++)
            CHECK(garden_fx_bullet_heading(extremes[x],extremes[y],128)==oracle(extremes[x],extremes[y],128),"signed32bit extremes agree with independent heading oracle without abs/multiply overflow");
    uint32_t seed=0x48454144U;
    for(unsigned i=0;i<8192U;i++) {
        seed=seed*1664525U+1013904223U;int32_t vx;memcpy(&vx,&seed,sizeof(vx));
        seed=seed*1664525U+1013904223U;int32_t vy;memcpy(&vy,&seed,sizeof(vy));
        CHECK(garden_fx_bullet_heading(vx,vy,256)==oracle(vx,vy,256),"independent full-range deterministic signed velocity samples retain exact nearest direction");
    }
    puts("PASS: literal axes/diagonals/boundaries/stationary wrap and independent libm velocity grid/extremes oracle");
}
static GardenGame game,saved;
static void build_pure(GardenFx *fx,GardenDraw *draws,unsigned capacity,unsigned alpha,unsigned *count)
{
    saved=game;garden_fx_observe(fx,&game);
    *count=garden_fx_build(fx,&game,draws,capacity,alpha);
    CHECK(!memcmp(&saved,&game,sizeof(game)),"production observer/build preserve every core byte: velocity/angle/spin/cord/colour/HP/collision/RNG/tick");
}
static void all_colours(void)
{
    static const int32_t dx[8]={1000,1000,0,-1000,-1000,-1000,0,1000};
    static const int32_t dy[8]={0,1000,1000,1000,0,-1000,-1000,-1000};
    garden_init(&game,12345U);game.player.active=0;
    unsigned count=0;
    for(unsigned type=1;type<=2;type++) for(unsigned colour=1;colour<=5;colour++) for(unsigned direction=0;direction<8;direction++) {
        GardenEnemyBullet *b=&game.enemy_bullets[count];
        *b=(GardenEnemyBullet){.active=1,.type=(uint8_t)type,.colour=(uint8_t)colour,
            .x=(int32_t)(40000U+count*5000U),.y=(int32_t)(90000U+count*1000U),
            .vx=dx[direction],.vy=dy[direction],.angle=(uint16_t)(((direction+3U)&7U)*128U),
            .cord_angle=768,.spin=-12,.cord_wait=(int16_t)(count&1U ? -1:20),.ttl=100};
        count++;
    }
    struct { GardenDraw d[80]; GardenDraw guard; } block;
    GardenDraw first[80],canary={-123,456,0x9999U,3U};unsigned n;
    GardenFx fx;garden_fx_reset(&fx);garden_fx_set_flags(&fx,0);
    for(unsigned alpha=0;alpha<2;alpha++) {
        block.guard=canary;build_pure(&fx,block.d,80,alpha,&n);
        CHECK(n==80U && !fx.stats.essential_dropped && !memcmp(&block.guard,&canary,sizeof(canary)),"all80 real-colour direction combinations retained without extra draws/capacity overwrite");
        for(unsigned i=0;i<n;i++) {
            const GardenEnemyBullet *b=&game.enemy_bullets[i];unsigned direction=i%8U;
            unsigned expected=(b->type==GARDEN_BULLET_DIAMOND ? GARDEN_ASSET_DIAMOND_BASE:GARDEN_ASSET_DART_BASE)+(b->colour-1U)*8U+direction;
            const garden_sprite_t *sprite=garden_asset(expected);
            CHECK(block.d[i].asset==expected && sprite,"production selects the original colour/type family at velocity octant, ignoring spin angle and activation-cord velocity");
            CHECK(block.d[i].x==b->x/GARDEN_GRAIN && block.d[i].y==b->y/GARDEN_GRAIN && block.d[i].alpha==32U,"true bullet position/atlas anchor policy/full opacity retained in both Alpha modes");
            CHECK(sprite->offset+(sprite->height-1U)*sprite->stride+sprite->width*2U<=GARDEN_ATLAS_BYTES,"selected frozen original direction descriptor remains within exact atlas source");
        }
        if(!alpha) memcpy(first,block.d,sizeof(first));
        else CHECK(!memcmp(first,block.d,sizeof(first)),"Alpha selection does not change real enemy bullet visuals or geometry");
    }
    game.paused=1U;build_pure(&fx,block.d,80,1,&n);
    CHECK(n==80U && !memcmp(first,block.d,sizeof(first)),"paused unchanged game has identical headings/positions/opacity and core clock");
    memset(game.enemy_bullets,0,sizeof(game.enemy_bullets));
    game.enemy_bullets[0]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=5,.x=100000,.y=200000,.angle=512,.cord_angle=128};
    build_pure(&fx,block.d,1,1,&n);CHECK(n==1U && block.d[0].asset==GARDEN_ASSET_DART_BASE+4U*8U+1U,"stationary newborn dart uses stable firing cord_angle rather than unrelated appearance spin");
    puts("PASS: complete production draw list, all80 original colour/type/direction IDs, true positions/anchors/Alpha/cord independence, pause/capacity/core-purity guards");
}
static unsigned enemy_start(const GardenGame *g)
{
    unsigned n=0;
    for(unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) n+=g->enemies[i].active!=0U;
    n+=g->boss.active!=0U;
    n+=g->player.active && (!g->player.grace || (g->tick&3U)<2U);
    for(unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) n+=g->player_bullets[i].active!=0U;
    return n;
}
static void real_spinning_diamond(void)
{
    garden_init(&game,12345U);unsigned found=GARDEN_MAX_ENEMY_BULLETS;
    for(unsigned tick=0;tick<6000U && found==GARDEN_MAX_ENEMY_BULLETS;tick++) {
        garden_step(&game,0);
        for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) {
            const GardenEnemyBullet *b=&game.enemy_bullets[i];
            if(b->active && b->type==GARDEN_BULLET_DIAMOND && b->spin && b->age>=10U &&
               (b->vx || b->vy) && garden_bullet_id(b->type,b->colour,b->angle)!=
               garden_bullet_id(b->type,b->colour,oracle(b->vx,b->vy,b->cord_angle)) &&
               b->x>20000 && b->x<620000 && b->y>20000 && b->y<460000) { found=i;break; }
        }
    }
    CHECK(found<GARDEN_MAX_ENEMY_BULLETS && game.boss_attack_shots[2]>0U,"unchanged real Boss attack3 naturally creates a spinning Diamond whose appearance angle disagrees with velocity");
    GardenEnemyBullet before=game.enemy_bullets[found];garden_step(&game,0);
    const GardenEnemyBullet *b=&game.enemy_bullets[found];
    CHECK(b->active && b->angle==((before.angle+before.spin)&1023U) && b->vx==before.vx && b->vy==before.vy,"true core keeps original rotating appearance angle and constant Diamond velocity");
    CHECK(b->x==before.x+b->vx && b->y==before.y+b->vy,"true per-step position delta equals vx/vy without any activation-cord offset");
    GardenFx fx;garden_fx_reset(&fx);garden_fx_set_flags(&fx,0);GardenDraw d[GARDEN_DRAW_MAX];unsigned n;
    build_pure(&fx,d,GARDEN_DRAW_MAX,1,&n);unsigned index=enemy_start(&game);
    for(unsigned i=0;i<found;i++) index+=game.enemy_bullets[i].active!=0U;
    CHECK(index<n && d[index].asset==garden_bullet_id(b->type,b->colour,oracle(b->vx,b->vy,b->cord_angle)),"production draws real spinning Diamond aligned to independent actual velocity heading");
    CHECK(d[index].asset!=garden_bullet_id(b->type,b->colour,b->angle),"real mismatch is repaired in presentation instead of modifying the core spin angle");
    printf("PASS: real-core attack3 spinning Diamond tick=%u slot=%u velocity=(%d,%d) core_angle=%u heading=%u; core bytes untouched\n",game.tick,found,b->vx,b->vy,b->angle,garden_fx_bullet_heading(b->vx,b->vy,b->cord_angle));
}
int main(void)
{
    directions();all_colours();real_spinning_diamond();
    printf("PASS: bullet-heading host contract checks=%u\n",checks);return 0;
}
