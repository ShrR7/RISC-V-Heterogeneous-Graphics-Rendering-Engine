/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production HUD rendered with production RGB565 fonts into host RAM.
 * This checks layout and stale-pixel removal, never board timing or MMIO.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_ui.h"
#include "garden_render.h"
#include "garden_assets.h"
#define WIDTH 1280
#define HEIGHT 720
static uint16_t buffers[2][HEIGHT][WIDTH],oracle[HEIGHT][WIDTH],before[HEIGHT][WIDTH];
static uint16_t (*pixels)[WIDTH];
static unsigned char atlas[GARDEN_ATLAS_BYTES];
typedef struct { int x,y,w,h;uint16_t color; } Box;
static Box boxes[64];
static unsigned capture,box_count,text_count,label_seen,old_label_seen;
static char hp_text[16];
static void bounds(int x,int y,int w,int h)
{ assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=WIDTH && y+h<=HEIGHT); }
void garden_render_box(int x,int y,int w,int h,uint16_t color)
{
    bounds(x,y,w,h);
    if(capture){assert(box_count<64U);boxes[box_count++]=(Box){x,y,w,h,color};}
    for(int row=y;row<y+h;row++)for(int col=x;col<x+w;col++)pixels[row][col]=color;
}
void garden_render_text_style(int x,int y,const char *text,unsigned style)
{
    int advance=style>=GARDEN_TEXT_LARGE_WHITE ? 16:8;
    unsigned font=style==GARDEN_TEXT_LARGE_CYAN ? GARDEN_ASSET_FONT_LARGE_CYAN_BASE:
        style==GARDEN_TEXT_LARGE_WHITE ? GARDEN_ASSET_FONT_LARGE_WHITE_BASE:
        style==GARDEN_TEXT_SMALL_CYAN ? GARDEN_ASSET_FONT_CYAN_BASE:GARDEN_ASSET_FONT_WHITE_BASE;
    if(!strcmp(text,"PLAYER HP"))label_seen++;
    if(!strcmp(text,"HULL / LIVES"))old_label_seen++;
    if(capture){text_count++;if(x==232 && y==262){assert(strlen(text)<sizeof(hp_text));strcpy(hp_text,text);}}
    while(*text){
        int glyph=garden_glyph(*text++);assert(glyph>=0);
        if(glyph!=garden_glyph(' ')){
            const garden_sprite_t *s=garden_asset(font+(unsigned)glyph-GARDEN_ASSET_FONT_WHITE_BASE);assert(s);
            bounds(x-s->anchor_x,y-s->anchor_y,s->width,s->height);
            for(unsigned row=0;row<s->height;row++)for(unsigned col=0;col<s->width;col++){
                unsigned off=s->offset+row*s->width*2U+col*2U;assert(off+1U<sizeof(atlas));
                uint16_t v=(uint16_t)(atlas[off]|(unsigned)atlas[off+1U]<<8);
                if(v!=GARDEN_KEY_COLOR)pixels[y-s->anchor_y+row][x-s->anchor_x+col]=v;
            }
        }
        x+=advance;
    }
}
static int health_region(int x,int y)
{ return (x>=232 && x<264 && y>=262 && y<278) || (x>=56 && x<263 && y>=286 && y<292); }
static void check_trace(unsigned hp,unsigned first)
{
    unsigned found_clear=0,found_bars=0;
    char expected[16];snprintf(expected,sizeof(expected),"%u",hp);assert(!strcmp(hp_text,expected));
    for(unsigned i=0;i<box_count;i++){
        Box b=boxes[i];
        if(b.x==232 && b.y==262){assert(b.w==32 && b.h==16 && b.color==GARDEN_UI_PANEL);found_clear++;}
        if(b.y==286){
            assert(found_bars<10U && b.x==56+(int)found_bars*21 && b.w==18 && b.h==6);
            assert(b.x+b.w<=264 && b.color==(found_bars<hp ? GARDEN_UI_CYAN:GARDEN_UI_EDGE));found_bars++;
        }
    }
    assert(found_clear==1U && found_bars==10U);
    if(!first)assert(box_count==11U && text_count==1U);
    for(unsigned i=0;i<10U;i++)for(int y=286;y<292;y++)for(int x=56+(int)i*21;x<74+(int)i*21;x++)
        assert(pixels[y][x]==(i<hp ? GARDEN_UI_CYAN:GARDEN_UI_EDGE));
}
static void save_panel(unsigned hp)
{
    char name[32];snprintf(name,sizeof(name),"hp_%02u.ppm",hp);FILE *f=fopen(name,"wb");assert(f);
    fprintf(f,"P6\n256 236\n255\n");
    for(int y=120;y<356;y++)for(int x=32;x<288;x++){
        uint16_t v=pixels[y][x];unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),
            (unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(atlas,1,sizeof(atlas),f)==sizeof(atlas) && fgetc(f)==EOF);assert(fclose(f)==0);
    GardenHud h={0};h.state="STAGE 1 WAVES";h.fps10=601;h.alpha_supported=1;h.alpha_enabled=1;
    const unsigned levels[]={10,9,3,1,0,10};
    garden_ui_hud_reset();
    for(unsigned buffer=0;buffer<2U;buffer++){pixels=buffers[buffer];garden_ui_static();}
    for(unsigned step=0;step<sizeof(levels)/sizeof(levels[0]);step++)for(unsigned buffer=0;buffer<2U;buffer++){
        pixels=buffers[buffer];memcpy(before,pixels,sizeof(before));h.lives=levels[step];
        capture=1;box_count=text_count=0;hp_text[0]=0;garden_ui_hud_cached(&h,buffer);capture=0;
        check_trace(h.lives,step==0U);
        if(step)for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++)
            if(!health_region(x,y))assert(pixels[y][x]==before[y][x]);
        pixels=oracle;garden_ui_static();garden_ui_hud(&h);
        assert(!memcmp(oracle,buffers[buffer],sizeof(oracle)));
        pixels=buffers[buffer];capture=1;box_count=text_count=0;garden_ui_hud_cached(&h,buffer);capture=0;
        assert(!box_count && !text_count && !memcmp(oracle,pixels,sizeof(oracle)));
        if(!buffer)save_panel(h.lives);
        printf("PASS HP=%u buffer=%u lit=%u unlit=%u number_clear=232,262,32,16 bars=10x18x6 stride21 cached_equals_full=1 unchanged_calls=0\n",
            h.lives,buffer,h.lives,10U-h.lives);
    }
    assert(label_seen && !old_label_seen);
    puts("PASS ten-HP HUD: two buffers, 10/9/3/1/0/10, actual glyphs, numeric shrink, no spill or stale pixels");
    return 0;
}
