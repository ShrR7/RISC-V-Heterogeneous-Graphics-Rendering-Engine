/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-only actual Boss generator and geometry contracts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/garden_core.c"
#include "garden_assets.h"
#include "garden_scene.h"
#include "garden_ordnance_view.h"
#include "boss_nose_reference.h"
static unsigned checks,cases,bullet_checks,laser_samples;
#define CHECK(c,m) do { checks++;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",__LINE__,m);exit(2);} } while(0)

static void bounds(const unsigned char *atlas)
{
    const garden_sprite_t *body=garden_asset(GARDEN_ASSET_BOSS1);
    int bottom=-1;
    for(unsigned y=0U;y<body->height;y++)for(unsigned x=0U;x<body->width;x++) {
        unsigned p=atlas[body->offset+y*garden_sprite_stride(body)+x*2U] |
                   (unsigned)atlas[body->offset+y*garden_sprite_stride(body)+x*2U+1U]<<8U;
        if(p!=GARDEN_KEY_COLOR && (int)y>bottom)bottom=(int)y;
    }
    CHECK(body->width==128U && body->height==132U && body->anchor_x==64 && body->anchor_y==66,"actual final normal Boss descriptor");
    CHECK(bottom==130 && bottom-body->anchor_y==GARDEN_BOSS_NOSE_Y_PX,"true final non-key bottom defines shared gameplay/render nose");
    unsigned at=body->offset+(unsigned)bottom*garden_sprite_stride(body)+(unsigned)body->anchor_x*2U;
    CHECK((atlas[at]|(unsigned)atlas[at+1U]<<8U)!=GARDEN_KEY_COLOR,"central shot outlet lies on retained nose pixel");
    for(unsigned i=0U;i<2U;i++) {
        const garden_sprite_t *pod=garden_asset(i ? GARDEN_ASSET_BOSS_WING_R:GARDEN_ASSET_BOSS_WING_L);
        at=pod->offset+(unsigned)pod->anchor_y*garden_sprite_stride(pod)+(unsigned)pod->anchor_x*2U;
        CHECK((atlas[at]|(unsigned)atlas[at+1U]<<8U)!=GARDEN_KEY_COLOR,"existing side laser source remains an actual barrel pixel");
    }
    printf("normal Boss descriptor=128x132 anchor=64,66 last_opaque=130 shot_offset=0,%d side_lasers=-42,19;+43,19\n",GARDEN_BOSS_NOSE_Y_PX);
}

static void generators(void)
{
    for(unsigned seed=1U;seed<=128U;seed++)for(unsigned attack=1U;attack<=3U;attack++)for(unsigned status=0U;status<4U;status++) {
        GardenGame game,reference;memset(&game,0,sizeof(game));
        game.rng=seed*23837U;game.mode=GARDEN_MODE_BOSS;
        game.boss=(GardenBoss){.active=1U,.x=170123+(int)seed*1000,.y=60123+(int)status*11000,
                              .status=(uint8_t)status,.attack=(uint8_t)attack,.volley_colour=(uint8_t)(1U+seed%5U)};
        reference=game;
        if(attack==1U){boss_attack1(&game);reference_boss_attack1(&reference);}
        else if(attack==2U){boss_attack2(&game);reference_boss_attack2(&reference);}
        else {boss_attack3(&game);reference_boss_attack3(&reference);}
        unsigned count=0U;
        for(unsigned i=0U;i<garden_enemy_scan_limit;i++)if(game.enemy_bullets[i].active) {
            GardenEnemyBullet *shot=&game.enemy_bullets[i],*prior=&reference.enemy_bullets[i];
            CHECK(shot->x==game.boss.x && shot->y==game.boss.y+GARDEN_BOSS_NOSE_Y_PX*GARDEN_GRAIN,"every real projectile starts at exact lower nose, never centre/ring radius");
            prior->x=shot->x;prior->y=shot->y;
            CHECK(!memcmp(shot,prior,sizeof(*shot)),"angle, velocity, acceleration, colour, spin, arming and lifetime unchanged");
            count++;bullet_checks++;
        }
        CHECK(count==(attack==1U ? 64U:attack==2U ? 9U:36U),"all complete pattern counts retained");
        CHECK(!memcmp(&game,&reference,sizeof(game)),"only real projectile origins differ from original generator; PRNG/counters unchanged");
        cases++;
    }
}

static void collision_uses_new_point(void)
{
    GardenGame game;memset(&game,0,sizeof(game));game.rng=123U;game.mode=GARDEN_MODE_BOSS;
    game.boss=(GardenBoss){.active=1U,.x=320000,.y=100000,.volley_colour=1U};boss_attack2(&game);
    GardenEnemyBullet born=game.enemy_bullets[0];
    memset(game.enemy_bullets,0,sizeof(game.enemy_bullets));born.age=GARDEN_BULLET_ARM_TICKS;born.cord_wait=-1;
    game.enemy_bullets[0]=born;game.lives=3U;
    game.player=(GardenPlayer){.active=1U,.x=born.x,.y=born.y};
    check_player_collisions(&game,born.x,born.y,GARDEN_LASER_OFF);
    CHECK(game.lives==2U && game.mode==GARDEN_MODE_GAMEOVER,"real collision consumes projectile at new physical nose, not an isolated visual offset");
}

static void lasers_unchanged(void)
{
    GardenGame game;memset(&game,0,sizeof(game));game.mode=GARDEN_MODE_BOSS;game.tick=100U;
    game.boss=(GardenBoss){.active=1U,.x=320000,.y=100000};laser_start(&game);
    CHECK(game.laser.x[0]==278000 && game.laser.x[1]==363000 && game.laser.y==119000,"laser remains at existing asymmetric side barrels");
    GardenScene scene;garden_scene_reset(&scene);
    for(unsigned t=0U;t<GARDEN_LASER_WARN_TICKS+GARDEN_LASER_FIRE_TICKS+GARDEN_LASER_FADE_TICKS;t++) {
        GardenSceneBossPose pose;GardenDraw beam[2];
        garden_scene_observe(&scene,&game);
        CHECK(garden_scene_boss_pose(&scene,&game,&pose)==1U,"actual composed Boss pose remains available");
        CHECK(garden_ordnance_laser_build(&game,beam,2U,1U)==2U,"both existing side beams remain visible");
        for(unsigned i=0U;i<2U;i++) {
            CHECK(pose.wings[i].x==game.laser.x[i]/GARDEN_GRAIN && pose.wings[i].y==game.laser.y/GARDEN_GRAIN,"displayed barrel tracks canonical laser origin through warn/fire/fade");
            CHECK(beam[i].x==pose.wings[i].x && beam[i].y==pose.wings[i].y,"visual and collision laser origins still coincide exactly");
        }
        CHECK(pose.cue.y==game.boss.y/GARDEN_GRAIN+GARDEN_BOSS_NOSE_Y_PX,"ordinary attack charge cue follows new nose outlet");
        run_laser(&game);game.tick++;laser_samples++;
    }
    CHECK(game.laser.phase==GARDEN_LASER_OFF,"unchanged40/40/12 laser phase durations");
}

int main(int argc,char **argv)
{
    CHECK(argc==2,"real atlas path required");
    unsigned char *atlas=malloc(GARDEN_ATLAS_BYTES);CHECK(atlas!=NULL,"host atlas buffer");
    FILE *file=fopen(argv[1],"rb");CHECK(file!=NULL,"actual final atlas opens");
    CHECK(fread(atlas,1U,GARDEN_ATLAS_BYTES,file)==GARDEN_ATLAS_BYTES && fgetc(file)==EOF,"descriptor and atlas size agree");fclose(file);
    bounds(atlas);generators();collision_uses_new_point();lasers_unchanged();
    printf("PASS boss_nose checks=%u generator_cases=%u projectile_births=%u laser_samples=%u board_acceptance=0\n",checks,cases,bullet_checks,laser_samples);
    free(atlas);return 0;
}
