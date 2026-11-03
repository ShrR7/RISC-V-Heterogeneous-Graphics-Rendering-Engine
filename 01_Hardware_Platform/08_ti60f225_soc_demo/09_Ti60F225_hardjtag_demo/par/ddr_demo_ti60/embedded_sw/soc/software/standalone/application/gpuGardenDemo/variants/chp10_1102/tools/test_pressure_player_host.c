/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host RGB565 background oracle for production pressure_player.c.
 * GPU/display/input are mocks, not FPGA timing or board acceptance. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pressure_player.h"
#include "garden_assets.h"
#include "garden_core.h"
#include "gpu_driver.h"
static uint16_t pixels[2][720*1280],baseline[2][720*1280],cache[2][4096],art[4096];
static garden_sprite_t sprite={0,40,36,20,18};
static unsigned checks,back,held,copies,blits;static int fail_copy,fail_blit;static uint32_t destination;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const garden_sprite_t*garden_asset(unsigned id){CHECK(id==GARDEN_ASSET_PLAYER);return &sprite;}
uint32_t display_back_addr(void){return back?0x400000:0x200000;}
uint32_t display_back_sel(void){return back;}
uint16_t pad_hw_held(uint32_t ms){(void)ms;return held;}
static uint16_t*pointer(uint32_t a){
 for(unsigned s=0;s<2;s++){uint32_t b=s?0x400000:0x200000;if(a>=b&&a<b+sizeof pixels[s])return pixels[s]+(a-b)/2;}
 if(a>=0x9c0000&&a<0x9c4000){unsigned s=(a-0x9c0000)>>13;return cache[s]+((a-0x9c0000)&8191)/2;}
 if(a>=GARDEN_ATLAS_ADDRESS&&a<GARDEN_ATLAS_ADDRESS+sizeof art)return art+(a-GARDEN_ATLAS_ADDRESS)/2;
 CHECK(0);return 0;
}
int gpu_copy(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds){
 copies++;CHECK(w==sprite.width&&h==sprite.height&&w<=64&&h<=64);
 CHECK((ss==2560&&ds==128)||(ss==128&&ds==2560));
 if(fail_copy){fail_copy=0;return GPU_EHW;}
 for(unsigned y=0;y<h;y++)memcpy(pointer(dst+y*ds),pointer(src+y*ss),w*2);
 return 0;
}
int gpu_bitblt(uint32_t src,uint32_t dst,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t key){
 blits++;destination=dst;CHECK(src==GARDEN_ATLAS_ADDRESS&&ss==w*2&&ds==2560&&key==GARDEN_KEY_COLOR);
 for(unsigned y=0;y<h;y++){uint16_t*s=pointer(src+y*ss),*d=pointer(dst+y*ds);for(unsigned x=0;x<w;x++)if(s[x]!=key)d[x]=s[x];if(fail_blit){fail_blit=0;return GPU_EHW;}}
 return 0;
}
static void restored(void){CHECK(!memcmp(pixels[back],baseline[back],sizeof pixels[back]));}
static unsigned pos_x(void){return ((destination-display_back_addr())%2560)/2;}
static unsigned pos_y(void){return (destination-display_back_addr())/2560;}
int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"usage: %s atlas.bin\n",argv[0]);return 2;}
 FILE *atlas=fopen(argv[1],"rb");CHECK(atlas);CHECK(fread(art,40U*36U*2U,1U,atlas)==1U);fclose(atlas);
 for(unsigned s=0;s<2;s++)for(unsigned i=0;i<720*1280;i++)pixels[s][i]=(uint16_t)(i*73+s*191);
 memcpy(baseline,pixels,sizeof pixels);
 sprite.width=65;CHECK(pressure_player_init()==GPU_EINVAL);CHECK(pressure_player_restore(display_back_addr(),0)==GPU_ESTATE);
 sprite.width=40;CHECK(!pressure_player_init());CHECK(pressure_player_restore(display_back_addr(),1)==GPU_ESTATE);
 CHECK(!pressure_player_restore(display_back_addr(),0));CHECK(!pressure_player_draw(display_back_addr(),0,1000));
 CHECK(pos_x()==620&&pos_y()==512);CHECK(pressure_player_draw(display_back_addr(),0,1010)==GPU_ESTATE);
 CHECK(!pressure_player_restore(display_back_addr(),0));restored();
 back=1;held=GARDEN_INPUT_RIGHT;CHECK(!pressure_player_draw(display_back_addr(),1,1016));CHECK(pos_x()==623);
 fail_copy=1;CHECK(pressure_player_restore(display_back_addr(),1)==GPU_EHW);CHECK(!pressure_player_restore(display_back_addr(),1));restored();
 back=0;held=0;CHECK(!pressure_player_draw(display_back_addr(),0,1032));CHECK(pos_x()==623);CHECK(!pressure_player_restore(display_back_addr(),0));restored();
 held=GARDEN_INPUT_RIGHT|GARDEN_INPUT_SLOW;CHECK(!pressure_player_draw(display_back_addr(),0,1048));CHECK(pos_x()>=624&&pos_x()<=625);CHECK(!pressure_player_restore(display_back_addr(),0));restored();
 held=GARDEN_INPUT_LEFT|GARDEN_INPUT_UP;
 for(unsigned i=0;i<180;i++){back=i&1;CHECK(!pressure_player_draw(display_back_addr(),back,1100+i*50));CHECK(pos_x()>=320&&pos_y()>=120);CHECK(!pressure_player_restore(display_back_addr(),back));restored();}
 CHECK(pos_x()==320&&pos_y()==120);
 held=GARDEN_INPUT_RIGHT|GARDEN_INPUT_DOWN;
 for(unsigned i=0;i<180;i++){back=i&1;CHECK(!pressure_player_draw(display_back_addr(),back,10200+i*50));CHECK(pos_x()<=920&&pos_y()<=564);CHECK(!pressure_player_restore(display_back_addr(),back));restored();}
 CHECK(pos_x()==920&&pos_y()==564);
 held=0;fail_blit=1;CHECK(pressure_player_draw(display_back_addr(),back,20000)==GPU_EHW);CHECK(!pressure_player_restore(display_back_addr(),back));restored();
 fail_copy=1;CHECK(pressure_player_draw(display_back_addr(),back,20016)==GPU_EHW);CHECK(!pressure_player_draw(display_back_addr(),back,20032));CHECK(!pressure_player_restore(display_back_addr(),back));restored();
 CHECK(!memcmp(pixels,baseline,sizeof pixels));
 printf("PASS pressure_player %u assertions; %u copies %u blits; alternating FB, exact RGB565 background, movement/slow/bounds, oversized rejection and copy/blit fault recovery\n",checks,copies,blits);return 0;
}
