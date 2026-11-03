/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production V gameplay tests. Includes core to exercise exact geometry and
 * finite effect lifecycles; no MMIO/renderer/board or FPS acceptance.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
unsigned legacy_crossing(int,int,int,int,int,unsigned,unsigned,unsigned);
static unsigned checks,runs,wins;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static GardenGame fixture(void)
{
    GardenGame g;garden_init(&g,12345U);g.tick=10U;g.demo_mode=0U;
    g.player.grace=0U;g.player.recycle=200U;return g;
}
static void enemy(GardenGame *g,unsigned i,int32_t x,int32_t y,int hp,unsigned type)
{g->enemies[i]=(GardenEnemy){.active=1U,.type=(uint8_t)type,.x=x,.y=y,.hp=hp,.phase=2U,.target_time=1000,.recycle=1000U};}
static unsigned crossing(int dx,int dy,int vx,int vy,int wait,unsigned grace,unsigned buttons,unsigned dart)
{
    GardenGame g=fixture();g.player.grace=(uint16_t)grace;
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.type=dart ? GARDEN_BULLET_DART:GARDEN_BULLET_DIAMOND,
        .x=g.player.x+dx,.y=g.player.y+dy,.vx=vx,.vy=vy,.ttl=500U,.cord_wait=(int16_t)wait,.age=59U};
    garden_step(&g,(uint16_t)buttons);
    if(g.reward.hits_taken)CHECK(g.lives==GARDEN_PLAYER_MAX_HP-1U && g.player.grace==GARDEN_PLAYER_HIT_GRACE_TICKS && !g.player.respawning);
    return g.reward.hits_taken;
}
static int clip(long double p,long double q,long double *lo,long double *hi)
{
    if(!p)return q>=0;
    long double ratio=q/p;
    if(p<0){if(ratio>*hi)return 0;if(ratio>*lo)*lo=ratio;}
    else{if(ratio<*lo)return 0;if(ratio<*hi)*hi=ratio;}
    return 1;
}
static unsigned oracle_box(int ax,int ay,int bx,int by)
{
    long double lo=0,hi=1,dx=bx-ax,dy=by-ay;
    return clip(-dx,ax+9000,&lo,&hi)&&clip(dx,9000-ax,&lo,&hi)&&
           clip(-dy,ay+9000,&lo,&hi)&&clip(dy,9000-ay,&lo,&hi);
}
static void swept_contract(void)
{
    CHECK(!legacy_crossing(0,-30000,0,60000,-1,0,0,0));
    CHECK(crossing(0,-30000,0,60000,-1,0,0,0)==1U);
    CHECK(!legacy_crossing(0,-30000,0,150000,-1,0,0,0));
    CHECK(crossing(0,-30000,0,150000,-1,0,0,0)==1U);
    /* A real diamond's2.2px/tick45degree velocity, not only high-speed data. */
    CHECK(xpart(384,2200)==-1555 && ypart(384,2200)==1555);
    CHECK(!legacy_crossing(9500,8000,-1555,1555,-1,0,0,0));
    CHECK(crossing(9500,8000,-1555,1555,-1,0,0,0)==1U);
    /* Legal original stopper dart speed5900 plus player movement6000. */
    CHECK(!legacy_crossing(12000,6000,0,5900,-1,0,GARDEN_INPUT_RIGHT,1));
    CHECK(crossing(12000,6000,0,5900,-1,0,GARDEN_INPUT_RIGHT,1)==1U);
    CHECK(!crossing(0,-30000,0,30000,20,0,0,0));
    CHECK(!crossing(0,-30000,0,30000,-1,80,0,0));
    CHECK(crossing(0,-30000,0,30000,-1,0,0,0)==1U);
    GardenGame armed=fixture();
    armed.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,
        .x=armed.player.x,.y=armed.player.y-30000,.vy=60000,.ttl=500U,.cord_wait=0,};
    garden_step(&armed,0U);
    CHECK(armed.enemy_bullets[0].cord_wait<0 && (armed.enemy_bullets[0].grazed&2U) && !armed.reward.hits_taken);
    armed.enemy_bullets[0].vy=-60000;garden_step(&armed,0U);
    CHECK(armed.reward.hits_taken==1U && armed.weapon_stats.swept_hits==1U);
    armed=fixture();armed.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,
        .x=armed.player.x,.y=armed.player.y-30000,.vy=30000,.ttl=500U,.cord_wait=0,};
    garden_step(&armed,0U);CHECK(armed.reward.hits_taken==1U);
    armed=fixture();armed.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,
        .x=armed.player.x+20000,.y=armed.player.y,.ttl=500U,.cord_wait=0,};
    garden_step(&armed,0U);CHECK(armed.reward.graze_count==1U && armed.enemy_bullets[0].grazed==3U);
    garden_step(&armed,0U);CHECK(armed.reward.graze_count==1U && armed.enemy_bullets[0].grazed==1U);
    CHECK(segment_box(10000,8000,8000,10000,-9000,-9000,9000,9000));
    CHECK(!segment_box(10001,8000,8001,10000,-9000,-9000,9000,9000));
    CHECK(!segment_box(10000,9000,9000,10000,-9000,-9000,9000,9000));
    uint32_t random=0x2468ace1U;
    for(unsigned i=0U;i<160000U;i++){
        int c[4];for(unsigned j=0U;j<4U;j++){random^=random<<13;random^=random>>17;random^=random<<5;c[j]=(int)(random%100001U)-50000;}
        CHECK(segment_box(c[0],c[1],c[2],c[3],-9000,-9000,9000,9000)==oracle_box(c[0],c[1],c[2],c[3]));
    }
    puts("PASS collision: pinned old-core real misses, legal2.2px diamond corner, dart+player relative crossing, early-outscreen cull, exact edge/one-millipixel near miss,160000independent Liang-Barsky cases; cord/grace untouched");
}
static void bomb_contract(void)
{
    GardenGame g=fixture();g.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,
        .x=100000,.y=100000,.ttl=500U,.cord_wait=-1};
    garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(g.reward.bombs==1U && g.reward.bombs_used==1U && g.ordnance.active);
    CHECK(!g.player.grace && !g.reward.bullets_cleared && g.enemy_bullet_count==1U);
    garden_step(&g,GARDEN_INPUT_BOMB);CHECK(g.reward.bombs_used==1U && g.reward.bombs==1U);
    int32_t y=g.ordnance.y;unsigned age=g.ordnance.age;
    garden_step(&g,GARDEN_INPUT_PAUSE);garden_step(&g,GARDEN_INPUT_BOMB);
    CHECK(g.paused && g.ordnance.y==y && g.ordnance.age==age && g.reward.bombs==1U);
    g=fixture();g.mode=GARDEN_MODE_BOSS;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=100000,.hp=50000,.max_hp=50000,.rhythm_state=GARDEN_BOSS_REST,.rhythm_ticks=1000U};
    enemy(&g,0,320000,100000,3800,GARDEN_ENEMY_STOPPER);
    enemy(&g,1,410000,100000,300,GARDEN_ENEMY_DARTER);
    enemy(&g,2,410001,100000,3800,GARDEN_ENEMY_STOPPER);
    g.ordnance=(GardenBomb){.x=320000,.y=109000,.active=1U};garden_step(&g,0U);
    CHECK(g.weapon_stats.bomb_impacts==1U && g.weapon_stats.bomb_targets==3U && g.weapon_stats.bomb_damage==1500U);
    CHECK(g.enemies[0].hp==3200 && !g.enemies[1].active && g.enemies[2].hp==3800 && g.boss.hp==49400);
    CHECK(g.combat.count==3U && !g.combat.dropped_total && g.score==23U);
    for(unsigned i=0U;i<16U;i++)run_ordnance(&g);
    CHECK(!g.ordnance.active && g.weapon_stats.bomb_damage==1500U);
    /* A normal lost life preserves a launched projectile; the final life
     * entering GAMEOVER must never leave a frozen flight/blast on screen. */
    g=fixture();g.ordnance=(GardenBomb){.x=320000,.y=250000,.active=1U};
    damage_player(&g);CHECK(g.lives==GARDEN_PLAYER_MAX_HP-1U && g.ordnance.active);
    g=fixture();g.lives=1U;g.ordnance=(GardenBomb){.x=320000,.y=250000,.active=1U};
    damage_player(&g);CHECK(g.mode==GARDEN_MODE_GAMEOVER && !g.ordnance.active && !g.ordnance.blast_ticks);
    garden_step(&g,GARDEN_INPUT_RESTART);CHECK(!g.ordnance.active && !g.ordnance.blast_ticks && g.reward.bombs==2U);
    /* Full supported one-step event bound:8blast targets+Boss+24projectiles. */
    g=fixture();g.mode=GARDEN_MODE_BOSS;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=100000,.hp=50000,.max_hp=50000,.rhythm_state=GARDEN_BOSS_REST,.rhythm_ticks=1000U};
    for(unsigned i=0U;i<8U;i++)enemy(&g,i,320000,100000,3800,GARDEN_ENEMY_STOPPER);
    for(unsigned i=0U;i<24U;i++)g.player_bullets[i]=(GardenPlayerBullet){.active=1U,.x=320000,.y=100000,.damage=95U};
    g.ordnance=(GardenBomb){.x=320000,.y=109000,.active=1U};garden_step(&g,0U);
    CHECK(GARDEN_COMBAT_EVENT_MAX==33U && g.combat.count==33U && g.combat.emitted_total==33U && !g.combat.dropped_total);
    CHECK(g.weapon_stats.bomb_targets==9U && g.weapon_stats.bomb_damage==5400U && g.enemies[0].hp==920);
    for(unsigned i=0U;i<33U;i++)CHECK(g.combat.items[i].damage==(i<9U ? 600U:95U));
    puts("PASS bomb: physical single-flight,600damage/true90px circle, no clear/grace, once-only impact, pause, exact33independent event bound");
}
static void heal_contract(void)
{
    for(unsigned life=1U;life<=GARDEN_PLAYER_MAX_HP;life++){
        GardenGame g=fixture();g.lives=(uint8_t)life;
        enemy(&g,0,320000,100000,500,GARDEN_ENEMY_STOPPER);
        GardenPlayerBullet shot={.x=320000,.y=100000,.damage=600U};hit_target(&g,&shot,0U);
        CHECK(g.weapon_stats.heals_spawned==1U && g.pickups[0].kind==GARDEN_PICKUP_HEAL);
        CHECK(!g.weapon_stats.heals_collected && g.lives==life);
        g.player.x=320000;g.player.y=250000;g.demo_mode=1U;
        for(unsigned i=0U;i<180U && g.pickups[0].active;i++)run_pickups(&g);
        CHECK(g.weapon_stats.heals_collected==1U && !g.pickups[0].active);
        CHECK(g.lives==(life<GARDEN_PLAYER_MAX_HP ? life+1U:GARDEN_PLAYER_MAX_HP) && g.weapon_stats.heals_restored==(life<GARDEN_PLAYER_MAX_HP));
        CHECK(g.reward.pickup_flash_kind==GARDEN_PICKUP_HEAL && g.reward.item_score_total==25U);
        for(unsigned i=0U;i<40U;i++)run_pickups(&g);
        CHECK(g.weapon_stats.heals_collected==1U && g.lives<=GARDEN_PLAYER_MAX_HP);
    }
    GardenGame g=fixture();g.lives=GARDEN_PLAYER_MAX_HP-1U;
    for(unsigned i=0U;i<8U;i++)drop_pickup(&g,g.player.x,g.player.y,GARDEN_PICKUP_HEAL);
    run_pickups(&g);CHECK(g.lives==GARDEN_PLAYER_MAX_HP && g.weapon_stats.heals_restored==1U && g.weapon_stats.heals_collected==8U);
    uint32_t best=g.reward.high_score;garden_step(&g,GARDEN_INPUT_RESTART);
    CHECK(g.lives==GARDEN_PLAYER_MAX_HP && !g.weapon_stats.heals_collected && g.reward.high_score==best);
    puts("PASS heal: STOPPER-only real drop, movement/contact required, one HP restored,10cap, full-health consumption/no extra life, repeat-safe/restart reset");
}
static GardenGame boss_fixture(void)
{
    GardenGame g=fixture();g.mode=GARDEN_MODE_BOSS;g.player.active=0U;
    g.boss=(GardenBoss){.active=1U,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.attack=2U,.rhythm_state=GARDEN_BOSS_TRANSIT};return g;
}
static void laser_contract(void)
{
    GardenGame g=boss_fixture();garden_step(&g,0U);
    CHECK(g.laser.phase==GARDEN_LASER_WARN && g.laser.ticks==40U && g.weapon_stats.laser_warnings==1U);
    int32_t x0=g.laser.x[0],x1=g.laser.x[1],y=g.laser.y;
    for(unsigned i=1U;i<40U;i++){garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_WARN && g.laser.ticks==40U-i);CHECK(g.laser.x[0]==x0 && g.laser.x[1]==x1 && g.laser.y==y);}
    garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_FIRE && g.laser.ticks==40U && g.weapon_stats.laser_shots==1U);
    for(unsigned i=1U;i<40U;i++){garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_FIRE && g.laser.ticks==40U-i);}
    garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_FADE && g.laser.ticks==12U);
    for(unsigned i=1U;i<12U;i++){garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_FADE);}
    garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_OFF);
    /* Body and the rendered wing mounts share the fixed warning anchors,
     * even when a normal transit is requested during the firing sequence. */
    g=boss_fixture();garden_step(&g,0U);
    x0=g.laser.x[0];x1=g.laser.x[1];y=g.laser.y;
    g.boss.move_ticks=40U;g.boss.target_x=g.boss.x+200000;g.boss.vx=1000;g.boss.vy=1000;
    for(unsigned i=0U;i<91U;i++){
        garden_step(&g,0U);
        CHECK(g.laser.phase!=GARDEN_LASER_OFF && g.laser.x[0]==x0 && g.laser.x[1]==x1 && g.laser.y==y);
        CHECK(g.boss.x==(x0+x1)/2 && g.boss.y==y-30000 && !g.boss.vx && !g.boss.vy);
    }
    garden_step(&g,0U);CHECK(g.laser.phase==GARDEN_LASER_OFF);
    g.boss.move_ticks=2U;g.boss.target_x=g.boss.x+200000;
    int32_t before_x=g.boss.x;garden_step(&g,0U);CHECK(g.boss.x>before_x);
    /* Capacity WAIT must still announce a full one-second warning on admit. */
    g=boss_fixture();for(unsigned i=0U;i<64U;i++)g.enemy_bullets[i]=(GardenEnemyBullet){.active=1U,.type=GARDEN_BULLET_DIAMOND,.x=100000,.y=100000,.ttl=500U,.cord_wait=100};
    garden_step(&g,0U);CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT && g.laser.phase==GARDEN_LASER_OFF);
    memset(g.enemy_bullets,0,sizeof(g.enemy_bullets));garden_step(&g,0U);
    CHECK(g.laser.phase==GARDEN_LASER_WARN && g.laser.ticks==40U);
    garden_step(&g,GARDEN_INPUT_PAUSE);CHECK(g.paused && g.laser.phase==GARDEN_LASER_OFF && !g.laser.ticks);
    for(unsigned phase=GARDEN_LASER_WARN;phase<=GARDEN_LASER_FADE;phase++)for(unsigned edge=0U;edge<2U;edge++){
        g=boss_fixture();g.player.active=1U;g.player.x=335000+(int32_t)edge;g.player.y=300000;
        g.laser=(GardenLaser){.x={320000,500000},.y=200000,.phase=(uint8_t)phase,.ticks=10U};
        garden_step(&g,0U);CHECK(g.weapon_stats.laser_hits==(phase==GARDEN_LASER_FIRE && !edge));
    }
    g=boss_fixture();g.player.active=1U;g.player.x=320000;g.player.y=300000;g.player.grace=80U;
    g.laser=(GardenLaser){.x={320000,500000},.y=200000,.phase=GARDEN_LASER_FIRE,.ticks=10U};
    garden_step(&g,0U);CHECK(!g.weapon_stats.laser_hits && g.lives==GARDEN_PLAYER_MAX_HP);
    GardenPlayerBullet shot={.x=g.boss.x,.y=g.boss.y,.damage=600U};g.boss.hp=1;hit_target(&g,&shot,GARDEN_MAX_ENEMIES);
    CHECK(g.mode==GARDEN_MODE_VICTORY && g.laser.phase==GARDEN_LASER_OFF);
    garden_step(&g,GARDEN_INPUT_RESTART);CHECK(g.laser.phase==GARDEN_LASER_OFF && !g.weapon_stats.laser_shots);
    /* Movement under the last warning must not be retroactively swept by the
     * first FIRE endpoint. Continuous FIRE, including its final fade-bound
     * interval, does sweep movement that genuinely occurred while active.
     */
    for(unsigned transition=0U;transition<4U;transition++){
        g=boss_fixture();g.player.active=1U;g.player.x=334000;g.player.y=300000;
        g.laser=(GardenLaser){.x={320000,500000},.y=200000,
            .phase=transition<2U ? GARDEN_LASER_WARN:GARDEN_LASER_FIRE,
            .ticks=transition==2U ? 10U:1U};
        garden_step(&g,transition==1U ? 0U:GARDEN_INPUT_RIGHT);
        CHECK(g.weapon_stats.laser_hits==(transition==0U ? 0U:1U));
        CHECK(g.reward.hits_taken==(transition==0U ? 0U:1U));
    }
    puts("PASS laser: fixed anchors, exact40warn/40fire/12fade, wait-admission warning, core+hitbox15px edge/1millipixel miss, warn/fade no damage,grace/pause/death/restart cleanup");
}
static void complete_runs(void)
{
    const uint32_t seeds[]={12345U,0x47617264U,1U,2U,3U,99999U,0xDEADBEEFU,0x2468ACE1U};
    for(unsigned i=0U;i<8U;i++){
        GardenGame g,b;garden_init(&g,seeds[i]);unsigned terminal=0U,won=0U;
        for(unsigned n=0U;n<12000U;n++){
            b=g;garden_step(&g,0U);
            CHECK(g.enemy_bullet_count<=80U && !g.dropped_enemy_bullets && !g.combat.dropped_total);
            CHECK(g.score==g.combat.hit_score_total+g.combat.kill_score_total+g.reward.graze_score_total+g.reward.item_score_total);
            CHECK(g.lives<=GARDEN_PLAYER_MAX_HP && g.reward.bombs+g.reward.bombs_used==2U && !g.reward.bullets_cleared);
            CHECK(g.combat.count<=33U && g.reward.high_score>=g.score);
            if((g.mode==GARDEN_MODE_VICTORY || g.mode==GARDEN_MODE_GAMEOVER)&&!g.end_ticks){terminal=1U;won=g.mode==GARDEN_MODE_VICTORY;wins+=won;
                printf("RUN seed=%u terminal=%s tick=%u score=%u bombs=%u impacts=%u damage=%u heal=%u/%u/%u laser=%u/%u/%u hits=%u swept=%u\n",seeds[i],won?"VICTORY":"GAMEOVER",g.tick,g.score,g.weapon_stats.bombs_launched,g.weapon_stats.bomb_impacts,g.weapon_stats.bomb_damage,g.weapon_stats.heals_spawned,g.weapon_stats.heals_collected,g.weapon_stats.heals_restored,g.weapon_stats.laser_warnings,g.weapon_stats.laser_shots,g.weapon_stats.laser_hits,g.reward.hits_taken,g.weapon_stats.swept_hits);}
            if(g.tick<b.tick){CHECK(terminal && g.reward.high_score>=b.score && g.reward.bombs==2U);
                if(!i)CHECK(won && b.weapon_stats.bombs_launched==2U && b.weapon_stats.bomb_impacts==2U && b.weapon_stats.bomb_damage>=1200U && b.weapon_stats.heals_collected>0U && b.weapon_stats.laser_shots>0U);
                printf("RESTART seed=%u best=%u\n",seeds[i],g.reward.high_score);runs++;break;}
        }
        CHECK(terminal && runs==i+1U);
    }
}
int main(void)
{
    swept_contract();bomb_contract();heal_contract();laser_contract();complete_runs();
    printf("RESULT checks=%u runs=%u wins=%u state_bytes=%u event_max=%u\n",checks,runs,wins,(unsigned)sizeof(GardenGame),(unsigned)GARDEN_COMBAT_EVENT_MAX);return 0;
}
