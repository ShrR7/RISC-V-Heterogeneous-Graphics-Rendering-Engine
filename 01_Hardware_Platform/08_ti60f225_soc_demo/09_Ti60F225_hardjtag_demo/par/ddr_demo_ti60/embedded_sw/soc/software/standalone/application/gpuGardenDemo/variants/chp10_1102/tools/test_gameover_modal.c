/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real stage result-card lifecycle; renderer primitives replaced by bounds
 * and text observation. No framebuffer, board, or performance claims. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_stage.h"
#include "garden_ui.h"
#include "garden_assets.h"

static unsigned checks,calls,saw_title,saw_restart,saw_best,saw_time;
static GardenRenderStats rs;
static GardenGame game;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
const GardenRenderStats *garden_render_stats(void){return &rs;}
void garden_render_box(int x,int y,int w,int h,uint16_t color)
{(void)color;calls++;CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=1280&&y+h<=720);}
void garden_render_text_style(int x,int y,const char *s,unsigned style)
{
    calls++;unsigned advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16U:8U;
    CHECK(x>=0&&y>=0&&(unsigned)x+strlen(s)*advance<=1280U&&y+24<=720);
    saw_title+=!strcmp(s,"GAME OVER");
    saw_restart+=!strcmp(s,"MINUS RESTART // AUTO AFTER 15S IDLE");
    saw_best+=!strcmp(s,"BEST 2000");saw_time+=!strcmp(s,"TIME SECONDS 42");
}
void garden_ui_stars(int x,int y,int w,int h){CHECK(x==320&&y==120&&w==640&&h==480);calls++;}
void garden_render_field_color(uint16_t color){(void)color;}

int main(void)
{
    GardenStage stage;
    memset(&game,0,sizeof game);game.mode=GARDEN_MODE_WAVES;game.tick=100U;
    garden_stage_reset(&stage);garden_stage_observe(&stage,&game);
    game.mode=GARDEN_MODE_GAMEOVER;game.tick++;game.score=1234U;
    game.reward.high_score=2000U;game.reward.run_ticks=42U*GARDEN_LOGIC_HZ;
    game.reward.hits_taken=1U;garden_stage_observe(&stage,&game);
    CHECK(garden_stage_modal_active(&stage,&game));
    GardenGame saved=game;
    for(unsigned b=0;b<2;b++){
        unsigned before=calls;
        CHECK(garden_stage_draw(&stage,&game,b)==1&&calls>before);
        CHECK(stage.cache[b].kind==GARDEN_STAGE_GAMEOVER&&stage.cache[b].score==1234U);
        before=calls;CHECK(garden_stage_draw(&stage,&game,b)==0&&calls==before);
        CHECK(garden_stage_prepare_frame(&stage,&game,b)==0);
    }
    CHECK(saw_title==2U&&saw_restart==2U&&saw_best==2U&&saw_time==2U);
    CHECK(!memcmp(&saved,&game,sizeof game));CHECK(stage.modal_buffers==3U);
    game.mode=GARDEN_MODE_WAVES;game.tick=0U;game.score=0U;
    garden_stage_observe(&stage,&game);CHECK(!garden_stage_modal_active(&stage,&game));
    for(unsigned b=0;b<2;b++){
        unsigned before=calls;
        CHECK(garden_stage_prepare_frame(&stage,&game,b)==1&&calls>before);
        before=calls;CHECK(garden_stage_prepare_frame(&stage,&game,b)==0&&calls==before);
        CHECK(garden_stage_draw(&stage,&game,b)==1&&stage.cache[b].kind==GARDEN_STAGE_FOOTER);
    }
    CHECK(stage.modal_buffers==0U&&stage.stats.modal_clears==2U);
    printf("PASS GAME OVER modal: %u assertions; immediate card, best/time, independent double-buffer cache and restart clearing\n",checks);
    return 0;
}
