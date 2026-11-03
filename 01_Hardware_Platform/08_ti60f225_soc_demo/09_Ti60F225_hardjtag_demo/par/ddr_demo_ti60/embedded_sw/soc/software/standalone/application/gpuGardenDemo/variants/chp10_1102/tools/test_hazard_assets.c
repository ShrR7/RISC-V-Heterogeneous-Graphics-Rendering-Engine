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
    CHECK(argc==3,"supply descriptor CSV and atlas path");
    CHECK(sizeof(garden_sprite_t)==8u&&offsetof(garden_sprite_t,anchor_x)==6u,"compact8byte ABI retained");
    CHECK(GARDEN_ASSET_COUNT==859u&&GARDEN_ASSET_ORDNANCE_BEAM_BASE==857u,"asset IDs retained");
    CHECK(GARDEN_BEAM_VISUAL_WIDTH==45u&&GARDEN_BEAM_CORE_WIDTH==13u&&GARDEN_BEAM_TEXTURE_PERIOD==24u,"beam renderer geometry contract");
    FILE *fp=fopen(argv[2],"rb");CHECK(fp,"atlas open");
    unsigned char *blob=malloc(GARDEN_ATLAS_BYTES);CHECK(blob,"host-only atlas allocation");
    CHECK(fread(blob,1,GARDEN_ATLAS_BYTES,fp)==GARDEN_ATLAS_BYTES,"full real atlas load");fclose(fp);
    FILE *dump=fopen(argv[1],"wb");CHECK(dump,"compiled descriptor CSV open");
    unsigned end=0u,bcount=0u;
    for(unsigned id=0u;id<GARDEN_ASSET_COUNT;++id){
        const garden_sprite_t*s=garden_asset(id);unsigned stride=garden_sprite_stride(s);
        CHECK(s&&s->width&&s->height&&s->width<=127u&&s->height<=127u,"dimensions fit uint8 ABI contract");
        CHECK(stride==2u*s->width&&!(s->offset&3u)&&s->offset>=end,"source offsets monotonic/aligned and tight");
        end=s->offset+stride*s->height;CHECK(end<=GARDEN_ATLAS_BYTES,"actual descriptor within atlas");
        CHECK(s->anchor_x>=0&&s->anchor_x<s->width&&s->anchor_y>=0&&s->anchor_y<s->height,"anchors fit signed ABI and source");
        CHECK(garden_asset_is_enemy_bullet(id)==(unsigned)bullet(id),"only640real bullets are classified dangerous sprite path");
        fprintf(dump,"%u,%u,%u,%u,%u,%d,%d\n",id,s->offset,s->width,s->height,stride,s->anchor_x,s->anchor_y);
        if(bullet(id)){
            unsigned count=0u,sumx=0u,sumy=0u;
            for(unsigned y=0u;y<s->height;++y)for(unsigned x=0u;x<s->width;++x){
                unsigned at=s->offset+y*stride+x*2u;unsigned color=blob[at]|((unsigned)blob[at+1u]<<8);
                if(color==65535u){++count;sumx+=x;sumy+=y;}
            }
            CHECK(count>0u,"every bullet contains authored exact white core");
            double cx=(double)sumx/count,cy=(double)sumy/count;
            double ex=(double)s->anchor_x-cx,ey=(double)s->anchor_y-cy;
            CHECK(ex>=-0.5000001&&ex<=0.5000001&&ey>=-0.5000001&&ey<=0.5000001,"actual anchor is nearest white-core centroid integer");
            CHECK(ex*ex+ey*ey<=0.5000001,"centroid Euclidean error <=sqrt(.5), never a glow/tail pivot");
            unsigned at=s->offset+(unsigned)s->anchor_y*stride+(unsigned)s->anchor_x*2u;
            CHECK((blob[at]|((unsigned)blob[at+1u]<<8))==65535u,"drawn physics point is inside actual white core");
            ++bcount;
        }
    }
    CHECK(bcount==640u,"all original8/fine64 directions x5palettes covered");
    unsigned seen[840]={0};
    for(unsigned type=1u;type<=2u;++type)for(unsigned color=1u;color<=5u;++color){
        for(unsigned angle=0u;angle<1024u;++angle){
            unsigned dir=((angle+8u)&1023u)/16u;
            unsigned expected=dir%8u ?280u+(type-1u)*280u+(color-1u)*56u+dir-dir/8u-1u:
                (type==2u?52u:12u)+(color-1u)*8u+dir/8u;
            unsigned id=garden_bullet_precision_id(type,color,angle);
            CHECK(id==expected,"640existing source selection and all1024angle rounding unchanged");
            if(!(angle%16u)){CHECK(id<840u&&!seen[id],"640direction/color IDs remain unique");seen[id]=1u;}
        }
    }
    for(unsigned id=857u;id<859u;++id){
        const garden_sprite_t*s=garden_asset(id);
        CHECK(s->width==45u&&s->height==120u&&s->anchor_x==22&&s->anchor_y==0,"laser atlas geometry matches renderer");
        for(unsigned y=0u;y<120u;++y)for(unsigned x=0u;x<45u;++x){
            unsigned at=s->offset+y*90u+x*2u,c=blob[at]|((unsigned)blob[at+1u]<<8);
            unsigned red=c>>11,green=(c>>5)&63u,blue=c&31u;
            int bright=red>=29u&&green>=56u&&blue>=27u;
            CHECK(bright==(x>=16u&&x<=28u),"exactly13bright core pixels and32low-luminance halo pixels per row");
            if(y>=24u){unsigned before=at-24u*90u;CHECK(blob[at]==blob[before]&&blob[at+1u]==blob[before+1u],"beam pattern repeats every24rows exactly");}
        }
    }
    CHECK(!garden_asset(859u)&&!garden_asset(UINT_MAX)&&!garden_asset_is_enemy_bullet(UINT_MAX),"invalid ID bounds safe");
    CHECK(!fclose(dump),"compiled CSV flush");free(blob);
    printf("PASS: hazard asset API checks=%u descriptor_bytes=%u count=%u atlas_bytes=%u\n",
        checks,(unsigned)sizeof(garden_sprite_t),GARDEN_ASSET_COUNT,GARDEN_ATLAS_BYTES);
    return 0;
}

