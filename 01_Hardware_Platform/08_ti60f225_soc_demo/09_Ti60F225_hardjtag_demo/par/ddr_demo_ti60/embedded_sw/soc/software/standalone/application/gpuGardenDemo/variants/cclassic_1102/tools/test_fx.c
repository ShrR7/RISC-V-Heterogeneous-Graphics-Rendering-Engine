/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host contract tests for production garden_fx; no MMIO or FPGA access.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_fx.h"
#include "garden_assets.h"

enum { WHITE_DARTER=266, WHITE_STOPPER=267, WHITE_BOSS=268, RING32=269, RING48=270 };
static unsigned checks;
#define CHECK(condition, message) do { checks++; if (!(condition)) { \
    fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,message); exit(2); } } while(0)

static void empty_game(GardenGame *g)
{
    memset(g,0,sizeof(*g));
    g->mode=GARDEN_MODE_WAVES; g->lives=3;
    g->player.active=1; g->player.x=80000; g->player.y=350000;
}

static void fx_begin(GardenFx *fx, GardenGame *g)
{
    garden_fx_reset(fx); garden_fx_set_flags(fx,GARDEN_FX_ALL);
    garden_fx_observe(fx,g);
}

static unsigned ghosts(const GardenDraw *draws,unsigned n)
{
    unsigned count=0;
    for (unsigned i=0;i<n;i++) if ((draws[i].asset==GARDEN_ASSET_PLAYER ||
        draws[i].asset==GARDEN_ASSET_PLAYER_FRAME1 || draws[i].asset==GARDEN_ASSET_PLAYER_FRAME2)
        && draws[i].alpha<32U) count++;
    return count;
}

static int has(const GardenDraw *draws,unsigned n,unsigned asset,int x,int y,unsigned alpha)
{
    for (unsigned i=0;i<n;i++) if (draws[i].asset==asset && draws[i].x==x &&
        draws[i].y==y && draws[i].alpha==alpha) return 1;
    return 0;
}

static void move_twice(GardenFx *fx,GardenGame *g)
{
    g->tick++; g->player.x+=12000; garden_fx_observe(fx,g);
    g->tick++; g->player.x+=12000; garden_fx_observe(fx,g);
}

static void white_selection(void)
{
    GardenGame g,saved; GardenFx fx; GardenDraw draws[GARDEN_DRAW_MAX];
    empty_game(&g); fx_begin(&fx,&g);
    g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.flash=3,.x=100000,.y=120000};
    g.enemies[1]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_STOPPER,.flash=1,.x=200000,.y=180000};
    g.boss=(GardenBoss){.active=1,.flash=2,.x=320000,.y=100000}; saved=g;
    unsigned n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(n==4,"flash replaces existing actors without adding draws");
    CHECK(has(draws,n,WHITE_DARTER,100,120,32),"darter uses white frame");
    CHECK(has(draws,n,WHITE_STOPPER,200,180,32),"stopper uses white frame");
    CHECK(has(draws,n,WHITE_BOSS,320,100,32),"Boss uses white frame");
    CHECK(garden_fx_stats(&fx)->flash_sprites==3,"flash statistics reflect three replacements");
    CHECK(!memcmp(&g,&saved,sizeof(g)),"draw builder does not change collision/game state");
    garden_fx_set_flags(&fx,0);
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(n==4 && has(draws,n,GARDEN_ASSET_DARTER1,100,120,32)
        && has(draws,n,GARDEN_ASSET_STOPPER1,200,180,32)
        && has(draws,n,GARDEN_ASSET_BOSS1,320,100,32),"FX off retains baseline actor IDs");
    CHECK(!garden_fx_stats(&fx)->flash_sprites,"FX off disables flash replacements");
    puts("PASS: white replacement IDs, baseline FX-off draw list, game immutability");
}

static void trails_and_lifecycle(void)
{
    GardenGame g; GardenFx fx; GardenDraw draws[GARDEN_DRAW_MAX], repeated[GARDEN_DRAW_MAX];
    empty_game(&g); fx_begin(&fx,&g); move_twice(&fx,&g);
    unsigned n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(ghosts(draws,n)==2,"two short ghosts after two distinct moves");
    int old=0,recent=0,live=-1,last_ghost=-1;
    for (unsigned i=0;i<n;i++) {
        if (draws[i].alpha==6 && draws[i].x==80 && draws[i].y==350) old++;
        if (draws[i].alpha==12 && draws[i].x==92 && draws[i].y==350) recent++;
        if (draws[i].alpha<32) last_ghost=(int)i;
        if (draws[i].asset==garden_player_id(g.tick) && draws[i].alpha==32) live=(int)i;
    }
    CHECK(old==1 && recent==1,"ghosts retain actual old positions and alpha6/12");
    CHECK(live>last_ghost,"ghosts precede bright live player");
    for (unsigned i=0;i<20;i++) {
        garden_fx_observe(&fx,&g);
        unsigned count=garden_fx_build(&fx,&g,repeated,GARDEN_DRAW_MAX,1);
        CHECK(count==n,"same-tick observation/render keeps draw count");
        for(unsigned j=0;j<n;j++) CHECK(draws[j].asset==repeated[j].asset &&
            draws[j].x==repeated[j].x && draws[j].y==repeated[j].y &&
            draws[j].alpha==repeated[j].alpha,"same-tick observation/render is idempotent");
    }
    g.paused=1;
    garden_fx_observe(&fx,&g);
    unsigned paused_count=garden_fx_build(&fx,&g,repeated,GARDEN_DRAW_MAX,1);
    CHECK(paused_count==n,"pause freezes and preserves current trail count");
    for(unsigned j=0;j<n;j++) CHECK(draws[j].asset==repeated[j].asset &&
        draws[j].x==repeated[j].x && draws[j].y==repeated[j].y &&
        draws[j].alpha==repeated[j].alpha,"pause freezes original trail positions");
    g.paused=0;
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,0);
    CHECK(!ghosts(draws,n),"alpha off suppresses trails");
    g.tick++; garden_fx_observe(&fx,&g); g.tick++; garden_fx_observe(&fx,&g);
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(!ghosts(draws,n),"stationary two steps converge to no trail");
    for (unsigned reason=0;reason<5;reason++) {
        empty_game(&g); fx_begin(&fx,&g); move_twice(&fx,&g);
        switch(reason) {
            case 0: g.tick=0; break;
            case 1: g.mode=GARDEN_MODE_BOSS; break;
            case 2: g.player.respawning=50; break;
            case 3: g.player.active=0; break;
            default: g.player.grace=80; g.tick=(g.tick&~3U)|2U; break;
        }
        garden_fx_observe(&fx,&g);
        n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
        CHECK(!ghosts(draws,n),"restart/mode/respawn/inactive/grace clears ghosts");
    }
    puts("PASS: true old-position alpha trails, render idempotence, stop/pause/restart/respawn lifecycle");
}

static void rings(void)
{
    GardenGame g; GardenFx fx; GardenDraw draws[GARDEN_DRAW_MAX];
    empty_game(&g); fx_begin(&fx,&g);
    g.effects[0]=(GardenEffect){.active=1,.age=11,.big=0,.x=100000,.y=100000};
    g.effects[1]=(GardenEffect){.active=1,.age=2,.big=0,.x=200000,.y=200000};
    g.effects[2]=(GardenEffect){.active=1,.age=0,.big=1,.x=300000,.y=300000};
    unsigned n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1), count=0;
    int last_ring=-1,first_explosion=-1;
    for (unsigned i=0;i<n;i++) {
        if (draws[i].asset==RING32 || draws[i].asset==RING48) { count++; last_ring=(int)i; }
        if (draws[i].asset>=GARDEN_ASSET_EXPLOSION0 && draws[i].asset<=GARDEN_ASSET_EXPLOSION3
            && first_explosion<0) first_explosion=(int)i;
    }
    CHECK(count==2 && garden_fx_stats(&fx)->rings==2,"kill rings bounded to two");
    CHECK(last_ring<first_explosion,"rings are behind bright explosion cores");
    int x200=0,x300=0;
    for (unsigned i=0;i<n;i++) if (draws[i].asset==RING32 || draws[i].asset==RING48) {
        x200+=draws[i].x==200; x300+=draws[i].x==300;
        CHECK(draws[i].alpha>0 && draws[i].alpha<32,"rings use global semi-transparent alpha");
    }
    CHECK(x200==1 && x300==1,"younger kills selected ahead of old ring");
    garden_fx_set_flags(&fx,0);
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(n==4 && !garden_fx_stats(&fx)->rings,"FX off retains player plus three baseline explosions");
    garden_fx_set_flags(&fx,GARDEN_FX_ALL);
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,0);
    CHECK(n==4 && !garden_fx_stats(&fx)->rings,"alpha off suppresses ring draws");
    for (unsigned i=0;i<3;i++) g.effects[i].age=12;
    n=garden_fx_build(&fx,&g,draws,GARDEN_DRAW_MAX,1);
    CHECK(n==4 && !garden_fx_stats(&fx)->rings,"rings end at age12 while baseline effects remain");
    puts("PASS: bounded youngest/big kill rings, alpha/FX-off behavior, age boundary");
}

static void full_pool_and_capacity(void)
{
    GardenGame g; GardenFx fx;
    struct { uint32_t before[4]; GardenDraw draws[GARDEN_DRAW_MAX]; uint32_t after[4]; } guarded;
    empty_game(&g); fx_begin(&fx,&g); move_twice(&fx,&g);
    for (unsigned i=0;i<GARDEN_MAX_ENEMIES;i++)
        g.enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=(int32_t)(i*17000),.y=100000};
    g.boss=(GardenBoss){.active=1,.x=320000,.y=100000};
    for (unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) g.player_bullets[i].active=1;
    for (unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) g.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=1};
    for (unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) g.effects[i]=(GardenEffect){.active=1,.age=0,.x=(int32_t)(i*15000),.y=180000};
    const unsigned essential=GARDEN_MAX_ENEMIES+2U+GARDEN_MAX_PLAYER_BULLETS+GARDEN_MAX_ENEMY_BULLETS+GARDEN_MAX_EFFECTS;
    for (unsigned cap=0;cap<=GARDEN_DRAW_MAX+1U;cap++) {
        memset(&guarded,0xA5,sizeof(guarded));
        unsigned limit=cap<GARDEN_DRAW_MAX-1U ? cap:GARDEN_DRAW_MAX-1U;
        unsigned n=garden_fx_build(&fx,&g,guarded.draws,cap,1);
        CHECK(n<=limit,"capacity includes renderer Alpha-panel reservation");
        CHECK(garden_fx_stats(&fx)->essential_dropped==(essential>limit ? essential-limit:0),"essential objects always have priority over decorations");
        const unsigned char *unused=(const unsigned char *)&guarded.draws[n];
        size_t bytes=(GARDEN_DRAW_MAX-n)*sizeof(guarded.draws[0])+sizeof(guarded.after);
        for (size_t i=0;i<bytes;i++) CHECK(unused[i]==0xA5,"no writes beyond returned draw count or capacity");
        for (unsigned i=0;i<4;i++) CHECK(guarded.before[i]==0xA5A5A5A5U,"leading capacity canary preserved");
        if (cap>=essential+4U) {
            CHECK(n==essential+4U,"full pool retains all234 essential plus2 ghosts and2 rings");
            CHECK(!garden_fx_stats(&fx)->essential_dropped,"full pool loses no essential objects");
        }
        if (limit==essential+1U) {
            CHECK(ghosts(guarded.draws,n)==1 && guarded.draws[0].alpha==12
                && guarded.draws[0].x==92 && guarded.draws[0].y==350,
                "one decoration slot retains brighter newer ghost, not an old ghost or ring");
        }
        if (limit<=essential) CHECK(!ghosts(guarded.draws,n) && !garden_fx_stats(&fx)->rings,"decorations drop before essential objects under pressure");
    }
    puts("PASS: full234-object pool, panel slot, every capacity0..241, canaries, essential priority");
}

static void core_noninterference(void)
{
    GardenGame baseline,with_fx; GardenFx fx; GardenDraw draws[GARDEN_DRAW_MAX];
    garden_init(&baseline,12345U); with_fx=baseline; fx_begin(&fx,&with_fx);
    for (unsigned step=0;step<12000U;step++) {
        uint16_t input=0;
        if (step%3500U==1200U || step%3500U==1230U) input|=GARDEN_INPUT_PAUSE;
        if (step%3500U==3000U) input|=GARDEN_INPUT_RESTART;
        garden_step(&baseline,input); garden_step(&with_fx,input);
        garden_fx_observe(&fx,&with_fx);
        garden_fx_build(&fx,&with_fx,draws,GARDEN_DRAW_MAX,step%2U);
        CHECK(!memcmp(&baseline,&with_fx,sizeof(baseline)),"FX observation/build preserves bit-exact core snapshot");
    }
    puts("PASS:12000 deterministic logic steps, pauses/restarts, complete core snapshots bit-exact");
}

static uint16_t pixel(const unsigned char *atlas,const garden_sprite_t *sprite,unsigned x,unsigned y)
{
    unsigned p=sprite->offset+y*sprite->stride+x*2U;
    CHECK(p+1U<GARDEN_ATLAS_BYTES,"sprite pixel stays in atlas");
    return (uint16_t)(atlas[p]|(unsigned)atlas[p+1U]<<8);
}

static void white_pixels(const char *path)
{
    unsigned char *atlas=malloc(GARDEN_ATLAS_BYTES); CHECK(atlas!=NULL,"allocate atlas");
    FILE *in=fopen(path,"rb"); CHECK(in!=NULL,"open atlas");
    CHECK(fread(atlas,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES,"read exact atlas bytes");
    CHECK(fgetc(in)==EOF,"no stale appended atlas bytes"); fclose(in);
    CHECK(GARDEN_ASSET_COUNT==280,"legacy FX/feedback plus six scene assets appended after266 baseline slots");
    for (unsigned i=0;i<3;i++) {
        const garden_sprite_t *base=garden_asset(GARDEN_ASSET_DARTER1+i),*white=garden_asset(WHITE_DARTER+i);
        CHECK(base && white,"white/base metadata valid");
        CHECK(base->width==white->width && base->height==white->height && base->anchor_x==white->anchor_x && base->anchor_y==white->anchor_y,"white frames retain exact bounds/anchor");
        unsigned clear=0,solid=0;
        for (unsigned y=0;y<base->height;y++) for (unsigned x=0;x<base->width;x++) {
            uint16_t a=pixel(atlas,base,x,y),b=pixel(atlas,white,x,y);
            if(a==GARDEN_KEY_COLOR) { CHECK(b==GARDEN_KEY_COLOR,"white preserves transparent ColorKey holes"); clear++; }
            else { CHECK(b==0xFFFFU,"every opaque white pixel is exact RGB565 white"); solid++; }
        }
        CHECK(clear && solid,"white has both transparent holes and opaque pixels");
    }
    free(atlas);
    puts("PASS: appended white masks preserve ColorKey holes, dimensions, anchors and exactRGB565white");
}

int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"Usage:test_fx ATLAS\n"); return 2; }
    white_selection(); trails_and_lifecycle(); rings(); full_pool_and_capacity(); core_noninterference(); white_pixels(argv[1]);
    printf("PASS: garden_fx host contract checks=%u; host-only, no performance or board acceptance claim\n",checks);
    return 0;
}
