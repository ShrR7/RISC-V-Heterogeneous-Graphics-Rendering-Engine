/* SPDX-License-Identifier: GPL-3.0-or-later
 * Contract tests using production feedback/FX/core/assets; no MMIO or board.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_feedback.h"
#include "garden_assets.h"

static unsigned checks;
#define CHECK(c,m) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,m); exit(2); } } while(0)

static void begin(GardenGame *g,GardenFx *fx,GardenFeedback *f)
{
    garden_init(g,12345U);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->player_bullets,0,sizeof(g->player_bullets));
    memset(g->enemy_bullets,0,sizeof(g->enemy_bullets));
    memset(g->effects,0,sizeof(g->effects));
    memset(&g->boss,0,sizeof(g->boss));
    g->tick=100U; g->mode=GARDEN_MODE_BOSS; g->demo_mode=0U;
    g->player=(GardenPlayer){.active=1,.x=320000,.y=400000,.recycle=500};
    garden_fx_reset(fx); garden_fx_observe(fx,g);
    garden_feedback_reset(f); garden_feedback_observe(f,g);
}

static unsigned asset_count(const GardenDraw *d,unsigned n,unsigned id)
{
    unsigned count=0; for(unsigned i=0;i<n;i++) count+=d[i].asset==id; return count;
}

static int has(const GardenDraw *d,unsigned n,unsigned id,int x,int y,unsigned alpha)
{
    for(unsigned i=0;i<n;i++) if(d[i].asset==id && d[i].x==x && d[i].y==y && d[i].alpha==alpha) return 1;
    return 0;
}

static void actual_core_hits(void)
{
    GardenGame g,saved; GardenFx fx; GardenFeedback f; GardenDraw d[GARDEN_DRAW_MAX];
    begin(&g,&fx,&f);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=190,.max_hp=190,.recycle=100,.phase_ticks=100};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=321000,.y=160000,.vy=-14000,.damage=95};
    garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
    garden_step(&g,0); saved=g; garden_feedback_observe(&f,&g); garden_fx_observe(&fx,&g);
    CHECK(g.boss.hp==95 && g.boss.active && !g.player_bullets[0].active,"real core produced an active Boss HP drop");
    CHECK(f.stats.hits==1 && f.stats.bullet_anchored_hits==1 && !f.stats.fallback_hits,"actual consumed bullet is corroborated");
    CHECK(f.sparks[0].x==321 && f.sparks[0].y==146,"impact uses consumed bullet post-step coordinates");
    unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,0);
    CHECK(has(d,n,GARDEN_ASSET_HIT_SPARK,321,146,32),"real Boss hit draws a white/gold impact");
    CHECK(!memcmp(&saved,&g,sizeof(g)),"observer and builder never change a real core snapshot");
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=323000,.y=160000,.vy=-14000,.damage=95};
    garden_feedback_observe(&f,&g); /* Same-tick new bullet must not change saved history. */
    garden_step(&g,0); garden_feedback_observe(&f,&g);
    CHECK(g.mode==GARDEN_MODE_VICTORY && g.score==1002 && !g.boss.active,"real core produced terminal Boss kill");
    CHECK(f.stats.hits==2 && f.sparks[0].active && f.sparks[0].age==0,"victory preserves the terminal impact");
    CHECK(f.stats.fallback_hits==1,"same-tick newly born bullet uses an explicitly counted fallback");
    for(unsigned type=GARDEN_ENEMY_DARTER;type<=GARDEN_ENEMY_STOPPER;type++) {
        begin(&g,&fx,&f);
        g.enemies[0]=(GardenEnemy){.active=1,.type=(uint8_t)type,.phase=2,.recycle=100,.target_time=100,
            .x=200000,.y=150000,.hp=95};
        int32_t impact_y=type==GARDEN_ENEMY_DARTER ? 154000:120000;
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=201000,.y=impact_y+14000,.vy=-14000,.damage=95};
        garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
        garden_step(&g,0); garden_feedback_observe(&f,&g);
        CHECK(!g.enemies[0].active && g.enemies[0].hp<=0,"real core enemy kill observed");
        CHECK(f.stats.hits==1 && f.stats.bullet_anchored_hits==1 && !f.stats.fallback_hits,"enemy kill requires score/flash/HP evidence");
        CHECK(f.sparks[0].x==201 && f.sparks[0].y==impact_y/GARDEN_GRAIN,"enemy consumed-bullet coordinate retained");
    }
    puts("PASS: actual core Boss HP-drop/kill and both enemy kills; consumed coordinates and honest fallback");
}

static void evidence_and_no_false_hits(void)
{
    GardenGame g; GardenFx fx; GardenFeedback f;
    for(unsigned reason=0;reason<8U;reason++) {
        begin(&g,&fx,&f);
        g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=150000,.y=150000,.hp=300};
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=150000,.y=170000,.vy=-14000,.age=10,.damage=95};
        garden_feedback_reset(&f); garden_feedback_observe(&f,&g); g.tick++;
        switch(reason) {
            case 0: g.enemies[0].flash=3; break; /* flash without HP change */
            case 1: g.enemies[0].active=0; g.enemies[0].y=600000; break; /* despawn */
            case 2: g.enemies[0].hp=400; break;
            case 3: g.player_bullets[0].active=0; g.player_bullets[0].age=91; break;
            case 4: g.enemies[0].active=0; g.enemies[0].hp=0; break;
            case 5: g.enemies[0].active=0; g.enemies[0].hp=0; g.enemies[0].flash=3; break;
            case 6: g.enemies[0].active=0; g.enemies[0].hp=0; g.score=20; break;
            default: g.score=20; break;
        }
        garden_feedback_observe(&f,&g);
        CHECK(!f.stats.hits && !f.stats.sparks_spawned,"flash, score, expired bullet or despawn alone never invents a hit");
    }
    begin(&g,&fx,&f);
    g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=150000,.y=150000,.hp=300};
    garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
    g.tick++; g.enemies[0].hp-=95; garden_feedback_observe(&f,&g);
    const garden_sprite_t *s=garden_asset(GARDEN_ASSET_DARTER1);
    CHECK(f.stats.hits==1 && f.stats.fallback_hits==1 && !f.stats.bullet_anchored_hits,"unmatched real HP drop is explicitly fallback");
    CHECK(f.sparks[0].x==150 && f.sparks[0].y==150+(int)s->height-s->anchor_y-1,"fallback anchors on target sprite lower edge");
    for(unsigned corrupt=0;corrupt<6;corrupt++) {
        begin(&g,&fx,&f);
        g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=1000};
        g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=320000,.y=146000,.vy=-14000,.age=10,.damage=95};
        garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
        g.tick++; g.boss.hp-=95; g.player_bullets[0].active=0;
        g.player_bullets[0].age++; g.player_bullets[0].y-=14000;
        switch(corrupt) {
            case 0: g.player_bullets[0].age++; break;
            case 1: g.player_bullets[0].damage++; break;
            case 2: g.player_bullets[0].x++; break;
            case 3: g.player_bullets[0].y++; break;
            case 4: g.player_bullets[0].active=1; break;
            default: g.tick++; break;
        }
        garden_feedback_observe(&f,&g);
        CHECK(f.stats.hits==1 && f.stats.fallback_hits==1 && !f.stats.bullet_anchored_hits,"invalid temporal/position/damage bullet evidence uses fallback rather than fake impact coordinate");
    }
    /* Two overlapping affected targets and one consumed bullet: slot order,
     * one use only; the second target must be honest fallback.
     */
    begin(&g,&fx,&f);
    for(unsigned i=0;i<2;i++) g.enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=200000,.y=150000,.hp=300};
    g.player_bullets[0]=(GardenPlayerBullet){.active=1,.x=200000,.y=168000,.vy=-14000,.age=10,.damage=95};
    garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
    g.tick++; for(unsigned i=0;i<2;i++) g.enemies[i].hp-=95;
    g.player_bullets[0].active=0; g.player_bullets[0].age++; g.player_bullets[0].y-=14000;
    garden_feedback_observe(&f,&g);
    CHECK(f.stats.hits==2 && f.stats.bullet_anchored_hits==1 && f.stats.fallback_hits==1,"one consumed bullet cannot be reused across targets");
    puts("PASS: evidence gates, no flash/score/despawn fake hits, invalid bullet fallback, one-use target matching");
}

static void lifespan_pause_restart(void)
{
    GardenGame g; GardenFx fx; GardenFeedback f; GardenDraw d[GARDEN_DRAW_MAX];
    begin(&g,&fx,&f);
    g.boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=1000};
    garden_feedback_reset(&f); garden_feedback_observe(&f,&g);
    for(unsigned i=0;i<5;i++) { g.tick++; g.boss.hp-=1; garden_feedback_observe(&f,&g); }
    CHECK(f.stats.sparks_spawned==5 && f.stats.spark_candidates_dropped==2,"newest three impacts evict older candidates explicitly");
    CHECK(f.sparks[0].age==0 && f.sparks[1].age==1 && f.sparks[2].age==2,"spark order newest first, different ages retained");
    GardenHitSpark frozen[3]; memcpy(frozen,f.sparks,sizeof(frozen));
    uint32_t hits=f.stats.hits; g.paused=1;
    for(unsigned i=0;i<30;i++) {
        garden_feedback_observe(&f,&g);
        unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,1);
        CHECK(!memcmp(frozen,f.sparks,sizeof(frozen)) && f.stats.hits==hits,"same-tick/pause observe/build freezes sparks and doesn't duplicate HP hit");
        CHECK(asset_count(d,n,GARDEN_ASSET_HIT_SPARK)==3,"pause maintains three visible impacts");
    }
    g.paused=0; memset(&f.sparks[1],0,sizeof(f.sparks[1])); memset(&f.sparks[2],0,sizeof(f.sparks[2]));
    for(unsigned age=0;age<=6;age++) {
        if(age) {g.tick++; garden_feedback_observe(&f,&g);}
        unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,0);
        unsigned alpha=age<2 ? 32U:age<4 ? 20U:10U;
        CHECK(asset_count(d,n,GARDEN_ASSET_HIT_SPARK)==(age<6),"spark lasts exactly six logic ticks");
        if(age<6) CHECK(has(d,n,GARDEN_ASSET_HIT_SPARK,frozen[0].x,frozen[0].y,alpha),"alpha follows 32/20/10 two-tick bands");
        n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,0,0);
        if(age<6) CHECK(has(d,n,GARDEN_ASSET_HIT_SPARK,frozen[0].x,frozen[0].y,32),"Alpha-off impact remains opaque without changing lifespan");
    }
    for(unsigned reset=0;reset<3;reset++) {
        begin(&g,&fx,&f); f.sparks[0]=(GardenHitSpark){100,100,1,0};
        if(reset==0) g.tick=1; else if(reset==1) g.mode=GARDEN_MODE_WAVES; else g.mode=GARDEN_MODE_GAMEOVER;
        garden_feedback_observe(&f,&g);
        CHECK(!f.sparks[0].active,"tick rollback/restart/other scene epoch clears impact");
    }
    begin(&g,&fx,&f); f.sparks[0]=(GardenHitSpark){100,100,1,0}; g.tick+=20;
    garden_feedback_observe(&f,&g); CHECK(!f.sparks[0].active,"large elapsed tick gap expires impact safely");
    puts("PASS: latest-three bound, six-tick alpha32/20/10 life, same-tick/pause freeze, restart/mode/gap expiry");
}

static void marker_and_edges(void)
{
    GardenGame g; GardenFx fx; GardenFeedback f; GardenDraw d[GARDEN_DRAW_MAX];
    begin(&g,&fx,&f);
    for(unsigned focus=0;focus<2;focus++) for(unsigned demo=0;demo<2;demo++)
    for(unsigned grace=0;grace<2;grace++) for(unsigned blink=0;blink<4;blink++) {
        g.demo_mode=(uint8_t)demo; g.player.grace=(uint16_t)grace; g.tick=100U+blink;
        unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,focus);
        unsigned wanted=focus||demo;
        unsigned id=grace ? GARDEN_ASSET_FOCUS_GRACE:GARDEN_ASSET_FOCUS_VULNERABLE;
        CHECK(f.stats.marker==wanted,"focus OR demo enables one marker even while grace sprite blinks");
        CHECK(asset_count(d,n,id)==wanted,"vulnerable/grace form follows current protection");
        if(wanted) CHECK(d[n-1].asset==id && has(d,n,id,320,400,32),"marker is opaque, paints last and uses exact core centre");
    }
    g.demo_mode=1; g.player.grace=0;
    const int centres[4][2]={{0,0},{639,0},{0,479},{639,479}};
    for(unsigned edge=0;edge<4;edge++) {
        g.player.x=centres[edge][0]*GARDEN_GRAIN; g.player.y=centres[edge][1]*GARDEN_GRAIN;
        unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,0);
        CHECK(has(d,n,GARDEN_ASSET_FOCUS_VULNERABLE,centres[edge][0],centres[edge][1],32),"field edge clipping must not move the true player centre");
    }
    g.player.respawning=1;
    unsigned n=garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,1);
    CHECK(!f.stats.marker && !asset_count(d,n,GARDEN_ASSET_FOCUS_VULNERABLE),"respawn hides collision marker");
    g.player.respawning=0; g.player.active=0;
    garden_feedback_build(&f,&fx,&g,d,GARDEN_DRAW_MAX,1,1);
    CHECK(!f.stats.marker,"inactive player has no marker");
    CHECK(!garden_feedback_build(&f,&fx,&g,NULL,240,1,1),"null draw destination safe");
    CHECK(!garden_feedback_build(NULL,&fx,&g,d,240,1,1),"null feedback safe");
    CHECK(!garden_feedback_build(&f,NULL,&g,d,240,1,1),"null FX safe");
    CHECK(!garden_feedback_build(&f,&fx,NULL,d,240,1,1),"null game safe");
    garden_feedback_observe(NULL,&g); garden_feedback_observe(&f,NULL); garden_feedback_reset(NULL);
    puts("PASS: focus/demo/protection/respawn combinations, grace blink independence, exact field-edge centre and null safety");
}

static void capacity_and_noninterference(void)
{
    GardenGame g; GardenFx fx; GardenFeedback f;
    struct {uint32_t before[4];GardenDraw d[GARDEN_DRAW_MAX];uint32_t after[4];} guarded;
    begin(&g,&fx,&f); g.demo_mode=1;
    g.tick++; g.player.x+=12000; garden_fx_observe(&fx,&g);
    g.tick++; g.player.x+=12000; garden_fx_observe(&fx,&g);
    for(unsigned i=0;i<GARDEN_MAX_ENEMIES;i++) g.enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.x=(int32_t)(i*20000),.y=100000};
    g.boss=(GardenBoss){.active=1,.x=320000,.y=100000};
    for(unsigned i=0;i<GARDEN_MAX_PLAYER_BULLETS;i++) g.player_bullets[i].active=1;
    for(unsigned i=0;i<GARDEN_MAX_ENEMY_BULLETS;i++) g.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=1};
    for(unsigned i=0;i<GARDEN_MAX_EFFECTS;i++) g.effects[i]=(GardenEffect){.active=1,.age=0,.x=(int32_t)(i*20000),.y=200000};
    for(unsigned i=0;i<3;i++) f.sparks[i]=(GardenHitSpark){(int16_t)(120+i*10),130,1,(uint8_t)i};
    const unsigned essential=234;
    for(unsigned cap=0;cap<=241;cap++) {
        memset(&guarded,0xA5,sizeof(guarded));
        unsigned limit=cap<240 ? cap:240;
        unsigned n=garden_feedback_build(&f,&fx,&g,guarded.d,cap,1,1);
        CHECK(n<=limit,"returned count bounded to requested capacity and240");
        CHECK(f.stats.essential_dropped==(essential>limit ? essential-limit:0),"all necessary sprites admitted before marker/sparks/legacy decorations");
        const unsigned char *unused=(const unsigned char*)&guarded.d[n];
        size_t bytes=(GARDEN_DRAW_MAX-n)*sizeof(guarded.d[0])+sizeof(guarded.after);
        for(size_t i=0;i<bytes;i++) CHECK(unused[i]==0xA5,"capacity canary and unused tail preserved");
        for(unsigned i=0;i<4;i++) CHECK(guarded.before[i]==0xA5A5A5A5U,"leading canary preserved");
        if(limit<=essential) CHECK(!f.stats.marker && !f.stats.sparks && !fx.stats.ghosts && !fx.stats.rings,"all decorative classes yield under essential pressure");
        if(limit>essential) {
            CHECK(f.stats.marker==1 && guarded.d[n-1].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"marker first decorative priority and last painting");
            unsigned sparks=limit-essential-1; if(sparks>3) sparks=3;
            CHECK(f.stats.sparks==sparks,"sparks next priority ahead of old decor");
            if(sparks) CHECK(has(guarded.d,n,GARDEN_ASSET_HIT_SPARK,120,130,32),"tight spark budget retains newest candidate");
        }
        if(limit==240) CHECK(n==240 && f.stats.sparks==3 && fx.stats.ghosts==2 && !fx.stats.rings,"full234pool keeps marker+3sparks+2ghosts, cuts rings first");
    }
    GardenGame base,with_feedback; garden_init(&base,12345U); with_feedback=base;
    garden_fx_reset(&fx); garden_fx_observe(&fx,&with_feedback);
    garden_feedback_reset(&f); garden_feedback_observe(&f,&with_feedback);
    GardenDraw d[GARDEN_DRAW_MAX];
    for(unsigned step=0;step<12000;step++) {
        uint16_t input=0;
        if(step%3500U==1200U || step%3500U==1230U) input|=GARDEN_INPUT_PAUSE;
        if(step%3500U==3000U) input|=GARDEN_INPUT_RESTART;
        garden_step(&base,input); garden_step(&with_feedback,input);
        garden_fx_observe(&fx,&with_feedback); garden_feedback_observe(&f,&with_feedback);
        garden_feedback_build(&f,&fx,&with_feedback,d,GARDEN_DRAW_MAX,step%2U,step%3U==0U);
        CHECK(!memcmp(&base,&with_feedback,sizeof(base)),"12000 production core snapshots bit-identical with feedback observer/build");
    }
    puts("PASS: capacities0..241 canaries, full234pool and essential/marker/spark priority;12000 complete core snapshots bit-identical");
}

int main(void)
{
    CHECK(GARDEN_ASSET_COUNT==280,"feedback marker/spark slots plus six scene decorations append after271");
    actual_core_hits(); evidence_and_no_false_hits(); lifespan_pause_restart(); marker_and_edges(); capacity_and_noninterference();
    printf("PASS: garden_feedback host contract checks=%u; host-only, no board/performance claim\n",checks);
    return 0;
}
