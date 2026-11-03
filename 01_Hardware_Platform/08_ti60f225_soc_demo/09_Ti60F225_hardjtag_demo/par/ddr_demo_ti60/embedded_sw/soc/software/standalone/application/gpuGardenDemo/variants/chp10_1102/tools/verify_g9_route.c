/* Independent host compositor for the production lossless mixed router. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "garden_assets.h"
#include "garden_render.h"
#include "garden_ui.h"
#include "gpu_alpha_ref.h"
#include "stage9_tiles.h"
#include "g9_memory.h"
#include "g9_route.h"

typedef struct {int16_t x,y,vx,vy;uint16_t sprite,flags;uint32_t reserved;} Desc;
static uint8_t original[GARDEN_ATLAS_BYTES];
static uint16_t legacy[640*480],mixed[640*480];
static GardenDraw list[GARDEN_DRAW_MAX];
static unsigned alpha_enabled=1,panel_visible=0;
static uint32_t random_state=0x13c697e1U;
static uint64_t checks,scenarios,compared,migrated,old_path,descriptors;
static unsigned max_desc,max_tiles,all_assets,cap2000,masked2000;
static uint64_t reasons[5];
#define REQUIRE(test) do{checks++;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(2);}}while(0)
int garden_render_alpha_enabled(void){return (int)alpha_enabled;}
int garden_render_panel_is_visible(void){return (int)panel_visible;}
unsigned garden_render_score_width(unsigned value){unsigned n=0;if(!value||value>32767U)return 0;do{n++;value/=10;}while(value);return 12+n*16;}
static unsigned rnd(unsigned n){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return n?random_state%n:0;}
static void put(uint16_t *fb,int x,int y,uint16_t value,unsigned a){
    if(x<0||x>=640||y<0||y>=480||!a||value==GARDEN_KEY_COLOR)return;
    fb[y*640+x]=gpu_alpha_rgb565(value,fb[y*640+x],a);
}
static void box(uint16_t *fb,int x,int y,int w,int h,uint16_t c){for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)put(fb,x+xx,y+yy,c,32);}
static void region(uint16_t *fb,unsigned id,int x,int y,unsigned a,unsigned sx,unsigned sy,unsigned w,unsigned h,int shift){
    const garden_sprite_t *s=garden_asset(id);REQUIRE(s!=NULL);
    for(unsigned yy=sy;yy<s->height&&yy<sy+h;yy++)for(unsigned xx=sx;xx<s->width&&xx<sx+w;xx++){
        unsigned off=s->offset+(yy*s->width+xx)*2U;
        uint16_t c=original[off]|(uint16_t)original[off+1]<<8;
        put(fb,x-s->anchor_x+(int)xx+shift,y-s->anchor_y+(int)yy,c,a);
    }
}
static void draw(uint16_t *fb,const GardenDraw *d){
    unsigned id=d->asset,a=alpha_enabled?d->alpha:32U;
    int x=d->x,y=d->y;
    if(id&GARDEN_DRAW_SCORE_BASE){
        unsigned v=id&GARDEN_DRAW_SCORE_MAX,n=0;char text[5];
        unsigned font=v==1?GARDEN_ASSET_FONT_LARGE_WHITE_BASE:GARDEN_ASSET_FONT_LARGE_CYAN_BASE;
        uint16_t c=v==1?0xffff:GARDEN_UI_CYAN;if(a<32)c=gpu_alpha_rgb565(c,0,a);
        if(a)box(fb,x+2,y+7,8,2,c);
        do{text[n++]=(char)('0'+v%10);v/=10;}while(v);
        x+=12;while(n){int glyph=garden_glyph(text[--n]);REQUIRE(glyph>=0);region(fb,font+(unsigned)glyph-GARDEN_ASSET_FONT_WHITE_BASE,x,y,a,0,0,256,256,0);x+=16;}
    }else if(id&GARDEN_DRAW_BAR_BASE){
        unsigned boss=id&GARDEN_DRAW_BAR_BOSS,fill=id&255U,width=boss?128:40,inner=width-2;
        uint16_t c=boss?GARDEN_UI_CYAN:0x5fea,border=0xad75,empty=0x1084;if(fill*3<inner)c=0xfd20;
        if(a<32){c=gpu_alpha_rgb565(c,0,a);border=gpu_alpha_rgb565(border,0,a);empty=gpu_alpha_rgb565(empty,0,a);}
        if(a){box(fb,x,y,(int)width,8,border);if(fill<inner)box(fb,x+1+(int)fill,y+1,(int)(inner-fill),6,empty);if(fill)box(fb,x+1,y+1,(int)fill,6,c);}
    }else if(id&GARDEN_DRAW_BANK_BASE){
        unsigned value=id&~GARDEN_DRAW_BANK_BASE,frame=value&3U;
        int bank=(int)((value>>2)&7U)-2;
        unsigned source=frame?GARDEN_ASSET_PLAYER_FRAME1+frame-1:GARDEN_ASSET_PLAYER;
        const garden_sprite_t*s=garden_asset(source);
        for(unsigned b=0;b<6;b++){unsigned first=b*s->height/6,end=(b+1)*s->height/6;int middle=(int)((first+end-1)/2);int shift=bank*((int)s->anchor_y-middle)/6;region(fb,source,x,y,a,0,first,256,end-first,shift);}
    }else if(id&GARDEN_DRAW_LASER_BASE){
        unsigned active=id&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE),source=GARDEN_ASSET_ORDNANCE_BEAM_BASE+((id&GARDEN_DRAW_LASER_FRAME)!=0);
        const garden_sprite_t*s=garden_asset(source);unsigned col=active?0:22,cols=active?45:1;
        unsigned phase=((id&GARDEN_DRAW_LASER_SCROLL_MASK)>>GARDEN_DRAW_LASER_SCROLL_SHIFT)*3U%24;
        for(int row=y<0?0:y;row<480;row++){unsigned sy=((unsigned)(row-y)+phase)%s->height;region(fb,source,x,row-(int)sy,a,col,sy,cols,1,0);}
    }else if(id&GARDEN_DRAW_FORM_BASE){
        unsigned source=garden_draw_form_source(id),r=id&GARDEN_DRAW_FORM_GROW?4:1;const garden_sprite_t*s=garden_asset(source);
        unsigned sx=(unsigned)s->anchor_x>r?(unsigned)s->anchor_x-r:0,sy=(unsigned)s->anchor_y>r?(unsigned)s->anchor_y-r:0;
        region(fb,source,x,y,a,sx,sy,r*2+1,r*2+1,0);
    }else if(id>=GARDEN_ASSET_EXPLOSION0&&id<=GARDEN_ASSET_EXPLOSION3){
        static const unsigned ink[4][4]={{5,5,21,21},{2,2,29,29},{3,3,31,31},{3,4,33,32}};unsigned k=id-GARDEN_ASSET_EXPLOSION0;
        region(fb,id,x,y,a,ink[k][0],ink[k][1],ink[k][2],ink[k][3],0);
    }else region(fb,id,x,y,a,0,0,256,256,0);
}
static void panel(uint16_t *fb){if(panel_visible&&alpha_enabled)for(unsigned y=96;y<128;y++)for(unsigned x=272;x<368;x++)put(fb,(int)x,(int)y,(uint16_t)(0x1020U+((x+y)&255U)),16);}
static void verify(unsigned count,unsigned enabled){
    REQUIRE(g9_route_prepare(list,count,enabled)==0);
    const G9RouteStats*s=g9_route_stats();REQUIRE(!s->errors);REQUIRE(s->tiles<=32);REQUIRE(s->descriptors<=2048);REQUIRE(s->migrated+s->legacy==s->enemies);
    REQUIRE(s->atlas_addr==G9_ATLAS0_ADDR||s->atlas_addr==G9_ATLAS1_ADDR);REQUIRE(s->desc_addr==G9_DESC0_ADDR||s->desc_addr==G9_DESC1_ADDR);
    unsigned skip=0,expect_desc=0;
    for(unsigned i=0;i<count;i++)if(g9_route_skip(i)){
        unsigned id=garden_draw_form_source(list[i].asset);REQUIRE(garden_asset_is_enemy_bullet(id));REQUIRE(list[i].asset==id&&list[i].alpha==32U&&enabled);
        const volatile Stage9TileAsset*t=stage9_tile_asset(id);expect_desc+=t->count;skip++;
    }
    REQUIRE(skip==s->migrated);REQUIRE(expect_desc==s->descriptors);REQUIRE(!g9_route_skip(count));
    for(unsigned i=0;i<640*480;i++)legacy[i]=mixed[i]=(uint16_t)(0x1082U+((i*73U)^(i>>7))%0x6000U);
    for(unsigned i=0;i<count;i++){draw(legacy,list+i);if(!g9_route_skip(i))draw(mixed,list+i);}
    panel(legacy);panel(mixed);
    const Desc*d=(const Desc*)(uintptr_t)s->desc_addr;const uint16_t*atlas=(const uint16_t*)(uintptr_t)s->atlas_addr;
    for(unsigned i=0;i<s->descriptors;i++){
        REQUIRE(d[i].sprite<s->tiles&&d[i].flags==1&&!d[i].vx&&!d[i].vy&&!d[i].reserved);
        for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){
            uint16_t color=atlas[d[i].sprite*256+y*16+x];
            int xx=(int)d[i].x-GARDEN_X0+(int)x,yy=(int)d[i].y-GARDEN_Y0+(int)y;
            /* Actual Stage9 clips to 1280x480, not the narrower game field.
             * Never let this host compositor's clip hide a leaking opaque pad. */
            if(color!=GARDEN_KEY_COLOR)REQUIRE(xx>=0&&xx<640&&yy>=0&&yy<360);
            if(yy>=360)continue;
            put(mixed,xx,yy,color,32);
        }
    }
    for(unsigned i=0;i<640*480;i++)if(legacy[i]!=mixed[i]){
        fprintf(stderr,"PIXEL scene=%llu count=%u enabled=%u x=%u y=%u legacy=%04X mixed=%04X migrated=%u desc=%u tiles=%u\n",(unsigned long long)scenarios,count,enabled,i%640,i/640,legacy[i],mixed[i],s->migrated,s->descriptors,s->tiles);exit(3);
    }
    scenarios++;compared+=640*480;migrated+=s->migrated;old_path+=s->legacy;descriptors+=s->descriptors;
    if(s->descriptors>max_desc)max_desc=s->descriptors;if(s->tiles>max_tiles)max_tiles=s->tiles;
    reasons[0]+=s->fallback_alpha;reasons[1]+=s->fallback_region;reasons[2]+=s->fallback_occlusion;reasons[3]+=s->fallback_tiles;reasons[4]+=s->fallback_desc;
}
static unsigned random_enemy(void){unsigned index=rnd(640);return index<80?12+index:280+index-80;}
static void random_cases(void){
    static const unsigned foreground[]={GARDEN_ASSET_PLAYER,GARDEN_ASSET_DARTER1,GARDEN_ASSET_STOPPER1,GARDEN_ASSET_BOSS1,GARDEN_ASSET_SHOT,GARDEN_ASSET_HIT_SPARK,GARDEN_ASSET_FOCUS_VULNERABLE,GARDEN_ASSET_FOCUS_GRACE,GARDEN_ASSET_ORDNANCE_HIT_BASE,GARDEN_ASSET_ORDNANCE_BLAST_BASE};
    for(unsigned test=0;test<240;test++){
        unsigned count=64+rnd(300);panel_visible=test%5==0;alpha_enabled=test%7!=0;
        for(unsigned i=0;i<count;i++){
            unsigned kind=rnd(100),id;
            if(kind<75)id=random_enemy();
            else if(kind<80)id=GARDEN_DRAW_SCORE_BASE|(1U+rnd(1900));
            else if(kind<85)id=GARDEN_DRAW_BAR_BASE|(rnd(2)?GARDEN_DRAW_BAR_BOSS:0)|rnd(36);
            else if(kind<90)id=GARDEN_DRAW_BANK_BASE|((rnd(5)<<2)|rnd(3));
            else if(kind<93)id=GARDEN_DRAW_LASER_BASE|13U|(rnd(2)?GARDEN_DRAW_LASER_ACTIVE:0)|(rnd(2)?GARDEN_DRAW_LASER_FRAME:0);
            else id=foreground[rnd(sizeof(foreground)/sizeof(*foreground))];
            unsigned alpha=rnd(10)?32:rnd(32);
            if(garden_asset_is_enemy_bullet(id)&&rnd(9)==0){id|=GARDEN_DRAW_FORM_BASE|(rnd(2)?GARDEN_DRAW_FORM_GROW:0);alpha=6+3*rnd(6);}
            list[i]=(GardenDraw){(int16_t)((int)rnd(760)-60),(int16_t)((int)rnd(560)-40),(uint16_t)id,(uint8_t)alpha};
        }
        verify(count,test%11!=0);
    }
}
int main(int argc,char**argv){
    REQUIRE(argc==3);void*m=VirtualAlloc((void*)(uintptr_t)0x800000,0x400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);REQUIRE(m==(void*)(uintptr_t)0x800000);
    FILE*f=fopen(argv[1],"rb");REQUIRE(f!=NULL);REQUIRE(fread((void*)(uintptr_t)ST9_TILES_BASE,1,ST9_TILES_BLOB_BYTES,f)==ST9_TILES_BLOB_BYTES);REQUIRE(fclose(f)==0);
    f=fopen(argv[2],"rb");REQUIRE(f!=NULL);REQUIRE(fread(original,1,sizeof(original),f)==sizeof(original));REQUIRE(fclose(f)==0);REQUIRE(g9_route_init()==0);
    for(unsigned id=0;id<GARDEN_ASSET_COUNT;id++)if(garden_asset_is_enemy_bullet(id)){
        list[0]=(GardenDraw){200,120,(uint16_t)id,32};verify(1,1);REQUIRE(g9_route_skip(0));all_assets++;
        const garden_sprite_t*s=garden_asset(id);
        for(unsigned edge=0;edge<4;edge++){
            int x=(edge&1U)?640-(int)s->width+s->anchor_x:s->anchor_x;
            int y=(edge&2U)?360-(int)s->height+s->anchor_y:s->anchor_y;
            list[0]=(GardenDraw){(int16_t)x,(int16_t)y,(uint16_t)id,32};verify(1,1);REQUIRE(g9_route_skip(0));
        }
    }
    /* Exact outer boundaries, forming alpha and source clip are routed/fallback honestly. */
    unsigned circle=garden_bullet_precision_id(2,2,0);const garden_sprite_t*cs=garden_asset(circle);
    for(int y=-20;y<=500;y+=5)for(int x=-20;x<=660;x+=40){list[0]=(GardenDraw){(int16_t)x,(int16_t)y,(uint16_t)circle,32};verify(1,1);if(y-cs->anchor_y+(int)cs->height>360)REQUIRE(!g9_route_skip(0));}
    for(unsigned age=0;age<6;age++){list[0]=(GardenDraw){200,120,(uint16_t)(circle|GARDEN_DRAW_FORM_BASE|(age>=3?GARDEN_DRAW_FORM_GROW:0)),(uint8_t)(6+age*3)};verify(1,1);REQUIRE(!g9_route_skip(0));}
    /* A zero-alpha foreground becomes opaque when legacy Alpha is disabled. */
    alpha_enabled=0;
    list[0]=(GardenDraw){200,120,(uint16_t)circle,32};
    list[1]=(GardenDraw){200,120,GARDEN_ASSET_HIT_SPARK,0};
    verify(2,1);REQUIRE(!g9_route_skip(0));
    random_cases();alpha_enabled=1;panel_visible=0;
    /* Later legacy enemy Alpha and a foreground force all earlier overlaps back. */
    list[0]=(GardenDraw){200,120,(uint16_t)circle,32};
    list[1]=(GardenDraw){200,120,(uint16_t)garden_bullet_precision_id(1,5,128),16};
    list[2]=(GardenDraw){200,120,GARDEN_ASSET_HIT_SPARK,32};
    verify(3,1);REQUIRE(!g9_route_skip(0)&&!g9_route_skip(1));
    for(unsigned i=0;i<1200;i++)list[i]=(GardenDraw){200,120,(uint16_t)garden_bullet_precision_id(2,3,0),32};
    verify(1200,1);REQUIRE(g9_route_stats()->descriptors==2048&&g9_route_stats()->fallback_desc>0);
    for(unsigned i=0;i<2000;i++)list[i]=(GardenDraw){(int16_t)(16+(i*17U)%608),(int16_t)(16+(i*31U)%320),(uint16_t)circle,32};
    verify(2000,1);REQUIRE(g9_route_stats()->migrated==2000&&g9_route_stats()->descriptors==2000&&g9_route_stats()->tiles==1);cap2000=1;
    list[2000]=(GardenDraw){210,90,GARDEN_DRAW_SCORE_BASE|190,32};
    list[2001]=(GardenDraw){380,90,GARDEN_DRAW_BAR_BASE|GARDEN_DRAW_BAR_BOSS|99,32};
    list[2002]=(GardenDraw){320,140,GARDEN_ASSET_FOCUS_VULNERABLE,32};
    verify(2003,1);REQUIRE(g9_route_stats()->migrated>0&&g9_route_stats()->legacy>0&&g9_route_stats()->migrated+g9_route_stats()->legacy==2000);masked2000=1;
    verify(2003,0);REQUIRE(g9_route_stats()->descriptors==0);
    g9_route_clear();REQUIRE(!g9_route_skip(0));REQUIRE(g9_route_init()==0);
    printf("{\"status\":\"PASS\",\"checks\":%llu,\"scenarios\":%llu,\"compared_pixels\":%llu,\"pixel_mismatches\":0,\"all_640_assets_exact\":%u,\"2000_one_tile_2000_desc\":%u,\"2000_with_foreground_no_loss\":%u,\"migrated_bullet_cases\":%llu,\"legacy_bullet_cases\":%llu,\"descriptor_cases\":%llu,\"max_descriptors\":%u,\"max_tiles\":%u,\"fallback_alpha\":%llu,\"fallback_region\":%llu,\"fallback_occlusion\":%llu,\"fallback_tiles\":%llu,\"fallback_descriptors\":%llu}\n",
      (unsigned long long)checks,(unsigned long long)scenarios,(unsigned long long)compared,all_assets,cap2000,masked2000,(unsigned long long)migrated,(unsigned long long)old_path,(unsigned long long)descriptors,max_desc,max_tiles,
      (unsigned long long)reasons[0],(unsigned long long)reasons[1],(unsigned long long)reasons[2],(unsigned long long)reasons[3],(unsigned long long)reasons[4]);
    REQUIRE(VirtualFree(m,0,MEM_RELEASE)!=0);return 0;
}
