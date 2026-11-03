/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_scene.h"
#include "garden_assets.h"
static unsigned checks,charge_samples,rest_samples,burst_samples,native_steps;
#define CHECK(c,m) do { checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m);exit(2);} }while(0)

static void start(GardenGame *g,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{
    garden_init(g,12345U);g->tick=100U;g->mode=GARDEN_MODE_BOSS;g->demo_mode=0U;g->player.active=0U;
    g->boss=(GardenBoss){.active=1,.x=320000,.y=120000,.hp=50000,.max_hp=50000,.attack=1,.rhythm_state=GARDEN_BOSS_TRANSIT};
    garden_scene_reset(s);garden_feedback_reset(f);garden_fx_reset(fx);
    garden_scene_observe(s,g);garden_feedback_observe(f,g);garden_fx_observe(fx,g);
}
static void observe(GardenGame *g,GardenScene *s,GardenFeedback *f,GardenFx *fx)
{garden_scene_observe(s,g);garden_feedback_observe(f,g);garden_fx_observe(fx,g);}
static unsigned find(const GardenDraw *d,unsigned n,unsigned id)
{for(unsigned i=0;i<n;i++) if(d[i].asset==id)return i;return n;}
static void rect_contains(const GardenSceneBossPose *p,const GardenDraw *d)
{
    const garden_sprite_t *a=garden_asset(d->asset);CHECK(a!=0,"known existing asset");
    CHECK(p->left<=d->x-a->anchor_x && p->top<=d->y-a->anchor_y &&
          p->right>=d->x-a->anchor_x+a->width && p->bottom>=d->y-a->anchor_y+a->height,
          "public exclusive bbox contains exact drawn shell footprint");
}
static void real_timing_and_render_interpolation(void)
{
    for(unsigned attack=1;attack<=3;attack++) for(unsigned band=0;band<3;band++) for(unsigned rate=40;rate<=60;rate+=20) {
        GardenGame g,snapshot;GardenScene s;GardenFeedback f;GardenFx fx;GardenSceneBossPose p;
        start(&g,&s,&f,&fx);g.boss.attack=(uint8_t)attack;g.boss.hp=band==0 ? 50000:band==1 ? 35000:20000;
        garden_step(&g,0);observe(&g,&s,&f,&fx);
        unsigned logic=0,previous_state=g.boss.rhythm_state,previous_open=0;
        for(unsigned frame=0;frame<rate*3U;frame++) {
            uint32_t us=frame*1000000U/rate,desired=us/25000U;
            while(logic<desired){garden_step(&g,0);observe(&g,&s,&f,&fx);logic++;}
            garden_scene_set_render_fraction(&s,(us%25000U)*256U/25000U);snapshot=g;
            CHECK(garden_scene_boss_pose(&s,&g,&p),"active Boss has pure render pose");
            unsigned state=g.boss.rhythm_state;
            if(state==GARDEN_BOSS_CHARGE){
                if(previous_state==state)CHECK(p.open_q8>=previous_open,"charge opens monotonically at40and60Hz");
                charge_samples++;
            }else if(state==GARDEN_BOSS_BURST){CHECK(p.open_q8==256U && p.cue_visible,"entire pulse train holds full open without four-tick wing toggles");burst_samples++;}
            else if(state==GARDEN_BOSS_REST){
                if(previous_state==state)CHECK(p.open_q8<=previous_open,"recovery closes monotonically at40and60Hz");
                if((unsigned)(g.boss.rest_ticks-g.boss.rhythm_ticks)>=16U)CHECK(!p.open_q8,"recovery fully closed by16ticks");
                rest_samples++;
            }
            CHECK(p.core.x==g.boss.x/1000 && p.core.y==g.boss.y/1000 && p.cue.x==p.core.x && p.cue.y==p.core.y+66,"true body/reactor/muzzle anchor never moves");
            CHECK(p.open_q8<=256U && p.core.alpha<=32U && p.cue.alpha<=32U,"integer animation and Alpha bounded");
            rect_contains(&p,&p.wings[0]);rect_contains(&p,&p.wings[1]);rect_contains(&p,&p.core);
            if(p.cue_visible)rect_contains(&p,&p.cue);
            CHECK(!memcmp(&g,&snapshot,sizeof(g)),"pose read has no native state, HP, RNG, hit or score write");
            previous_state=state;previous_open=p.open_q8;
        }
    }
    CHECK(charge_samples && rest_samples && burst_samples,"all real rhythm animation phases exercised");
    puts("PASS: real3patterns x3HPbands x40/60Hz, monotonic smoothstep charge/recovery, train hold and pure exact shared pose");
}
static void follow_pause_restart_and_local_flash(void)
{
    GardenGame g;GardenScene s;GardenFeedback f;GardenFx fx;GardenSceneBossPose p,saved;
    GardenDraw d[240];start(&g,&s,&f,&fx);
    g.boss.rhythm_state=GARDEN_BOSS_REST;g.boss.rest_ticks=80;g.boss.rhythm_ticks=40;
    for(unsigned i=0;i<8;i++){g.tick++;g.boss.x+=2000;observe(&g,&s,&f,&fx);}
    garden_scene_set_render_fraction(&s,0);garden_scene_boss_pose(&s,&g,&p);
    CHECK(p.wings[0].x==g.boss.x/1000-55-4 && p.wings[1].x==g.boss.x/1000+55-6,"left/right pods follow true centre by2/3logic ticks");
    garden_scene_set_render_fraction(&s,128);garden_scene_boss_pose(&s,&g,&p);
    CHECK(p.wings[0].x==g.boss.x/1000-55-3 && p.wings[1].x==g.boss.x/1000+55-5,"render fraction interpolates follower histories between logic samples");
    g.tick++;g.boss.x+=100000;observe(&g,&s,&f,&fx);garden_scene_boss_pose(&s,&g,&p);
    CHECK(p.wings[0].x==g.boss.x/1000-55-6 && p.wings[1].x==g.boss.x/1000+55-6,"cosmetic lag clamps6px even after abrupt true target movement");
    garden_scene_set_render_fraction(&s,163);garden_scene_boss_pose(&s,&g,&saved);
    garden_step(&g,GARDEN_INPUT_PAUSE);observe(&g,&s,&f,&fx);
    for(unsigned i=0;i<5;i++){garden_scene_set_render_fraction(&s,i*60U);garden_scene_boss_pose(&s,&g,&p);CHECK(!memcmp(&p,&saved,sizeof(p)),"pause freezes all cosmetic interpolation, breathing and history");}
    garden_step(&g,GARDEN_INPUT_RESTART);observe(&g,&s,&f,&fx);
    CHECK(!garden_scene_boss_pose(&s,&g,&p) && !s.render_fraction && !s.history_valid,"restart clears stale Boss pose/history/fraction");
    start(&g,&s,&f,&fx);g.boss.flash=3;g.enemies[0]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.flash=3,.x=100000,.y=100000,.hp=300};
    unsigned n=garden_scene_build(&s,&f,&fx,&g,d,240,1,0);
    CHECK(find(d,n,GARDEN_ASSET_BOSS1)<n && find(d,n,GARDEN_ASSET_FLASH_BOSS1)==n,"Boss material survives actual core flash3");
    CHECK(find(d,n,GARDEN_ASSET_FLASH_DARTER1)<n,"ordinary enemy flash policy retained");
    GardenSceneBossPose a,b;garden_scene_set_render_fraction(&s,0);garden_scene_boss_pose(&s,&g,&a);
    g.tick+=32;garden_scene_observe(&s,&g);garden_scene_boss_pose(&s,&g,&b);
    CHECK(a.core.alpha!=b.core.alpha && a.wings[0].y!=b.wings[0].y,"idle reactor brightness and wing lift actually animate");
    garden_scene_set_enabled(&s,0);CHECK(!garden_scene_boss_pose(&s,&g,&p),"disabled scene exposes no cosmetic geometry");
    garden_scene_set_enabled(&s,1);garden_scene_observe(&s,&g);garden_scene_boss_pose(&s,&g,&p);
    CHECK(p.wings[0].x==g.boss.x/1000-55,"reenable snaps histories to present centre instead of stale delayed pose");
    puts("PASS:2/3tick follower history,6px clamp, render interpolation, pause/restart, material preservation and idle breathing");
}
static void capacity_and_backgrounds(void)
{
    GardenGame g,snapshot;GardenScene s;GardenFeedback f;GardenFx fx;
    struct {uint32_t before[4];GardenDraw d[240];uint32_t after[4];} box;
    start(&g,&s,&f,&fx);g.demo_mode=1;g.player=(GardenPlayer){.active=1,.x=320000,.y=400000};
    for(unsigned i=0;i<8;i++)g.enemies[i]=(GardenEnemy){.active=1,.type=GARDEN_ENEMY_DARTER,.hp=300,.x=(int32_t)i*20000,.y=100000};
    for(unsigned i=0;i<24;i++)g.player_bullets[i]=(GardenPlayerBullet){.active=1,.x=(int32_t)i*20000,.y=200000};
    for(unsigned i=0;i<192;i++)g.enemy_bullets[i]=(GardenEnemyBullet){.active=1,.type=GARDEN_BULLET_DART,.colour=3,.x=(int32_t)(i%32)*20000,.y=(int32_t)(i/32)*30000};
    for(unsigned i=0;i<8;i++)g.effects[i]=(GardenEffect){.active=1,.big=1,.x=200000,.y=200000};
    observe(&g,&s,&f,&fx);snapshot=g;
    for(unsigned alpha=0;alpha<2;alpha++)for(unsigned capacity=0;capacity<=241;capacity++) {
        memset(&box,0xA5,sizeof(box));unsigned limit=capacity<240 ? capacity:240;
        unsigned n=garden_scene_build(&s,&f,&fx,&g,box.d,capacity,alpha,1);
        CHECK(n<=limit && s.stats.essential_dropped==(limit<234 ? 234-limit:0),"all capacities preserve234essentials before decoration");
        for(unsigned i=0;i<4;i++)CHECK(box.before[i]==0xA5A5A5A5U && box.after[i]==0xA5A5A5A5U,"capacity canaries untouched");
        for(unsigned i=limit;i<240;i++){GardenDraw blank;memset(&blank,0xA5,sizeof(blank));CHECK(!memcmp(&blank,&box.d[i],sizeof(blank)),"no draw write past caller capacity");}
        if(limit>=235)CHECK(f.stats.marker==1 && box.d[n-1].asset==GARDEN_ASSET_FOCUS_VULNERABLE,"true collision marker beats cosmetic shell and remains foreground");
        for(unsigned i=0;i<n;i++)CHECK(box.d[i].asset<GARDEN_ASSET_COUNT && box.d[i].alpha<=32U,"only existing sprites and legal Alpha admitted");
        CHECK(!memcmp(&g,&snapshot,sizeof(g)),"capacity truncation cannot mutate game");
    }
    start(&g,&s,&f,&fx);g.mode=GARDEN_MODE_WAVES;g.boss.active=0;g.tick=100;observe(&g,&s,&f,&fx);
    GardenDraw first[8],later[8];unsigned n=garden_scene_build(&s,&f,&fx,&g,first,8,1,0);
    CHECK(n==8 && s.stats.backgrounds==8,"unchanged eight small backdrop draws");
    g.tick+=4;observe(&g,&s,&f,&fx);n=garden_scene_build(&s,&f,&fx,&g,later,8,1,0);
    unsigned far=find(first,8,GARDEN_ASSET_GARDEN_FAR),near=find(first,8,GARDEN_ASSET_GARDEN_STREAK);
    CHECK(later[find(later,n,GARDEN_ASSET_GARDEN_FAR)].y-first[far].y==2 &&
          later[find(later,n,GARDEN_ASSET_GARDEN_STREAK)].y-first[near].y==6,"real20/60px-per-second parallax on40Hz logic");
    CHECK(first[far].alpha==24 && first[near].alpha==26,"existing botanical motifs become more readable without more layers");
    garden_scene_set_render_fraction(&s,999);CHECK(s.render_fraction==255,"fraction clamps without overflow");
    CHECK(!garden_scene_boss_pose(0,&g,0),"null pose inputs safe");garden_scene_set_render_fraction(0,0);
    puts("PASS:0..241draw budgets/canaries, essential/marker priority, exact20/60px/s original8draw background");
}
static void native_trajectory_and_actual_shots(void)
{
    GardenGame g,reference,snapshot;GardenScene s;GardenFeedback f;GardenFx fx;GardenDraw d[240];
    garden_init(&g,12345);reference=g;garden_scene_reset(&s);garden_feedback_reset(&f);garden_fx_reset(&fx);
    observe(&g,&s,&f,&fx);unsigned observed=0;
    for(unsigned step=0;step<18000;step++) {
        uint16_t input=step==600 || step==611 ? GARDEN_INPUT_PAUSE:step==7400 ? GARDEN_INPUT_RESTART:0;
        unsigned before=g.boss_attack_shots[0]+g.boss_attack_shots[1]+g.boss_attack_shots[2];
        garden_step(&g,input);garden_step(&reference,input);snapshot=g;observe(&g,&s,&f,&fx);
        unsigned after=g.boss_attack_shots[0]+g.boss_attack_shots[1]+g.boss_attack_shots[2];
        if(after>before)observed+=after-before;
        for(unsigned frame=0;frame<2;frame++) {
            garden_scene_set_render_fraction(&s,frame*128U);
            unsigned n=garden_scene_build(&s,&f,&fx,&g,d,240,1,1);
            CHECK(n<=240 && !s.stats.essential_dropped,"natural animation keeps all actual actors");
            CHECK(!memcmp(&g,&snapshot,sizeof(g)) && !memcmp(&g,&reference,sizeof(g)),"18000native snapshots byte unchanged by presentation including RNG/HP/score/collision");
            unsigned boss=find(d,n,GARDEN_ASSET_BOSS1);
            if(g.boss.active)CHECK(boss<n && d[boss].x==g.boss.x/1000 && d[boss].y==g.boss.y/1000,"actual Boss body never follows cosmetic lag or turns white");
        }
        native_steps++;
    }
    CHECK(observed && s.stats.attack_shots_observed==observed,"only actual original core shot-counter deltas drive recoil stats");
    printf("PASS: native_steps=%u actual_shots=%u charge_samples=%u rest_samples=%u burst_samples=%u\n",native_steps,observed,charge_samples,rest_samples,burst_samples);
}
int main(void)
{
    real_timing_and_render_interpolation();follow_pause_restart_and_local_flash();capacity_and_backgrounds();native_trajectory_and_actual_shots();
    printf("PASS: animation scene checks=%u scene_bytes=%u\n",checks,(unsigned)sizeof(GardenScene));return 0;
}
