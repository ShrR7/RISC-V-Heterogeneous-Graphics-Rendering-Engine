/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "garden_assets.h"
static unsigned checks;
#define CHECK(c,m) do{++checks;if(!(c)){fprintf(stderr,"FAIL line%u:%s\n",(unsigned)__LINE__,m);exit(2);}}while(0)
static int bullet(unsigned id){return(id>=12u&&id<92u)||(id>=280u&&id<840u);}
int main(int argc,char **argv)
{
    CHECK(argc==3,"supply descriptor CSV and actual atlas path");
    CHECK(sizeof(garden_sprite_t)==8u&&offsetof(garden_sprite_t,anchor_x)==6u,"compact8byte ABI retained");
    CHECK(GARDEN_ASSET_COUNT==859u&&GARDEN_ASSET_ORDNANCE_BEAM_BASE==857u,"numeric asset IDs stable");
    CHECK(GARDEN_BEAM_VISUAL_WIDTH==45u&&GARDEN_BEAM_CORE_WIDTH==13u&&GARDEN_BEAM_TEXTURE_PERIOD==24u,"beam renderer shape retained");
    CHECK(GARDEN_ATLAS_BYTES<=0xba000u,"atlas before scratch budget");
    FILE *fp=fopen(argv[2],"rb");CHECK(fp,"actual atlas open");
    unsigned char *blob=malloc(GARDEN_ATLAS_BYTES);CHECK(blob,"host atlas allocation");
    CHECK(fread(blob,1,GARDEN_ATLAS_BYTES,fp)==GARDEN_ATLAS_BYTES,"full atlas bytes load");
    CHECK(fgetc(fp)==EOF,"atlas exact byte size");fclose(fp);
    FILE *dump=fopen(argv[1],"wb");CHECK(dump,"compiled descriptor CSV open");
    unsigned bcount=0u;
    for(unsigned id=0u;id<GARDEN_ASSET_COUNT;++id){
        const garden_sprite_t*s=garden_asset(id);CHECK(s,"valid ID descriptor");
        unsigned stride=garden_sprite_stride(s);
        CHECK(s->width&&s->height,"nonempty uint8 source dimensions;132px Boss allowed");
        CHECK(stride==2u*s->width&&!(s->offset&3u),"tight aligned rows;identical orb payload aliases allowed");
        CHECK(s->offset+stride*s->height<=GARDEN_ATLAS_BYTES,"descriptor source bounded by actual atlas");
        CHECK(s->anchor_x>=0&&s->anchor_x<s->width&&s->anchor_y>=0&&s->anchor_y<s->height,"signed anchors fit source");
        CHECK(garden_asset_is_enemy_bullet(id)==(unsigned)bullet(id),"only640existing enemy projectile IDs use hazardous path");
        fprintf(dump,"%u,%u,%u,%u,%u,%d,%d\n",id,s->offset,s->width,s->height,stride,s->anchor_x,s->anchor_y);
        if(bullet(id)){
            unsigned count=0u,sumx=0u,sumy=0u;
            for(unsigned y=0u;y<s->height;++y)for(unsigned x=0u;x<s->width;++x){
                unsigned at=s->offset+y*stride+x*2u;unsigned color=blob[at]|((unsigned)blob[at+1u]<<8);
                if(color==65535u){++count;sumx+=x;sumy+=y;}
            }
            CHECK(count>0u,"every danger sprite contains exact white core");
            double cx=(double)sumx/count,cy=(double)sumy/count;
            double ex=(double)s->anchor_x-cx,ey=(double)s->anchor_y-cy;
            CHECK(ex>=-0.5000001&&ex<=0.5000001&&ey>=-0.5000001&&ey<=0.5000001,"nearest authored-white centroid anchor");
            CHECK(ex*ex+ey*ey<=0.5000001,"white centroid error <=sqrt(.5)");
            unsigned at=s->offset+(unsigned)s->anchor_y*stride+(unsigned)s->anchor_x*2u;
            CHECK((blob[at]|((unsigned)blob[at+1u]<<8))==65535u,"physics point renders inside real white core");
            ++bcount;
        }
    }
    CHECK(bcount==640u,"64directions x5palettes x2stable source families");
    unsigned seen[840]={0};
    for(unsigned type=1u;type<=2u;++type)for(unsigned color=1u;color<=5u;++color){
        for(unsigned angle=0u;angle<1024u;++angle){
            unsigned dir=((angle+8u)&1023u)/16u;
            unsigned expected=dir%8u?280u+(type-1u)*280u+(color-1u)*56u+dir-dir/8u-1u:
                (type==2u?52u:12u)+(color-1u)*8u+dir/8u;
            unsigned id=garden_bullet_precision_id(type,color,angle);
            CHECK(id==expected,"all1024angles preserve64direction ID helper mapping");
            if(!(angle%16u)){CHECK(id<840u&&!seen[id],"numeric IDs remain distinct even when round pixels alias");seen[id]=1u;}
        }
    }
    const garden_sprite_t*shot=garden_asset(4u);
    CHECK(shot->width==5u&&shot->height==19u&&shot->anchor_x==2&&shot->anchor_y==1,"new player lance preserves old logical shot reference");
    const unsigned players[]={0u,264u,265u};
    for(unsigned n=0;n<3u;++n){const garden_sprite_t*s=garden_asset(players[n]);
        CHECK(s->width==40u&&s->height==36u&&s->anchor_x==20&&s->anchor_y==18,"all tail/bank player sources are new40x36 delta");}
    const garden_sprite_t*body=garden_asset(3u);CHECK(body->width==128u&&body->height==132u&&body->anchor_x==64&&body->anchor_y==66,"cruiser body source geometry");
    for(unsigned n=0;n<2u;++n){const garden_sprite_t*s=garden_asset(274u+n);
        CHECK(s->width==44u&&s->height==72u&&s->anchor_x==(int)(22u+n)&&s->anchor_y==69,"source-cropped gun pod anchored at real muzzle");
        unsigned at=s->offset+(unsigned)s->anchor_y*garden_sprite_stride(s)+(unsigned)s->anchor_x*2u;
        CHECK((blob[at]|((unsigned)blob[at+1u]<<8))!=GARDEN_KEY_COLOR,"muzzle point sits on actual opaque barrel");}
    for(unsigned id=857u;id<859u;++id){const garden_sprite_t*s=garden_asset(id);
        CHECK(s->width==45u&&s->height==120u&&s->anchor_x==22&&s->anchor_y==0,"laser texture geometry retained");
        for(unsigned y=0u;y<120u;++y)for(unsigned x=0u;x<45u;++x){
            unsigned at=s->offset+y*90u+x*2u,c=blob[at]|((unsigned)blob[at+1u]<<8);
            int bright=(c>>11)>=29u&&((c>>5)&63u)>=56u&&(c&31u)>=27u;
            CHECK(bright==(x>=16u&&x<=28u),"13pxbright beam core matches harmless halo boundaries");}}
    CHECK(!garden_asset(859u)&&!garden_asset(UINT_MAX)&&!garden_asset_is_enemy_bullet(UINT_MAX),"invalid IDs reject safely");
    CHECK(!fclose(dump),"compiled CSV flush");free(blob);
    printf("PASS: variety asset API checks=%u descriptor_bytes=%u count=%u atlas_bytes=%u\n",checks,(unsigned)sizeof(garden_sprite_t),GARDEN_ASSET_COUNT,GARDEN_ATLAS_BYTES);
    return 0;
}
