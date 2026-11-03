/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stddef.h>
#include "garden_assets.h"
static unsigned checks;
#define CHECK(c,m) do {++checks;if(!(c)){fprintf(stderr,"FAIL line%u: %s\n",(unsigned)__LINE__,m);exit(2);}}while(0)
int main(int argc,char **argv)
{
    CHECK(sizeof(garden_sprite_t)==8u,"real ABI descriptor size is 8 bytes");
    CHECK(offsetof(garden_sprite_t,offset)==0u && offsetof(garden_sprite_t,width)==4u &&
          offsetof(garden_sprite_t,height)==5u && offsetof(garden_sprite_t,anchor_x)==6u &&
          offsetof(garden_sprite_t,anchor_y)==7u,"no hidden ABI padding in 859 records");
    garden_sprite_t signed_probe={0u,1u,1u,-128,-1};
    CHECK(signed_probe.anchor_x==-128 && signed_probe.anchor_y==-1,"anchor storage is signed int8");
    CHECK(GARDEN_ASSET_COUNT==859u && GARDEN_ASSET_ORDNANCE_HIT_BASE==840u &&
          GARDEN_ASSET_ORDNANCE_HIT_COUNT==6u && GARDEN_ASSET_ORDNANCE_BLAST_BASE==846u &&
          GARDEN_ASSET_ORDNANCE_BLAST_COUNT==8u && GARDEN_ASSET_ORDNANCE_BOMB_BASE==854u &&
          GARDEN_ASSET_ORDNANCE_BOMB_COUNT==2u && GARDEN_ASSET_ORDNANCE_HEAL==856u &&
          GARDEN_ASSET_ORDNANCE_BEAM_BASE==857u && GARDEN_ASSET_ORDNANCE_BEAM_COUNT==2u,"fixed renderer asset contract");
    unsigned previous_end=0u;
    FILE *dump=argc>1 ? fopen(argv[1],"wb") : 0;
    CHECK(argc<=1 || dump,"open metadata dump");
    for(unsigned id=0;id<GARDEN_ASSET_COUNT;++id){
        const garden_sprite_t *s=garden_asset(id);unsigned stride=garden_sprite_stride(s);
        CHECK(s && s->width && s->height && s->width<=127u && s->height<=127u,"bounded uint8 dimensions");
        CHECK(stride==2u*s->width && !(s->offset&3u) && s->offset>=previous_end,"tight aligned monotonic rows");
        CHECK(s->offset+stride*s->height<=GARDEN_ATLAS_BYTES,"every C API source fits atlas DDR extent");
        CHECK(s->anchor_x>=0 && s->anchor_x<s->width && s->anchor_y>=0 && s->anchor_y<s->height,"signed anchors preserve valid source geometry");
        CHECK(garden_asset_is_enemy_bullet(id)==((id>=12u&&id<92u)||(id>=280u&&id<840u)),"new effects never enter enemy bullet crop path");
        previous_end=s->offset+stride*s->height;
        if(dump) fprintf(dump,"%u,%u,%u,%u,%u,%d,%d\n",id,s->offset,s->width,s->height,stride,s->anchor_x,s->anchor_y);
    }
    CHECK(!garden_asset(GARDEN_ASSET_COUNT) && !garden_asset(UINT_MAX),"out of bounds returns null");
    CHECK(!garden_asset_is_enemy_bullet(UINT_MAX) && !garden_asset_is_enemy_bullet(GARDEN_ASSET_COUNT),"invalid assets never classified bullet");
    unsigned seen[840]={0};
    for(unsigned type=1u;type<=2u;++type) for(unsigned col=1u;col<=5u;++col){
        for(unsigned bin=0u;bin<64u;++bin){
            unsigned id=garden_bullet_precision_id(type,col,bin*16u);
            CHECK(id<840u&&!seen[id]&&garden_asset_is_enemy_bullet(id),"old640heading sources remain unique");
            seen[id]=1;
        }
        for(unsigned angle=0u;angle<1024u;++angle){
            unsigned dir=((angle+8u)&1023u)/16u;
            unsigned expected=dir%8u ?280u+(type-1u)*280u+(col-1u)*56u+dir-dir/8u-1u :
                (type==2u?52u:12u)+(col-1u)*8u+dir/8u;
            CHECK(garden_bullet_precision_id(type,col,angle)==expected,"all1024old angle mappings unchanged");
        }
    }
    for(unsigned id=840u;id<846u;++id){
        const garden_sprite_t*s=garden_asset(id);
        CHECK(s->width==32u&&s->height==32u&&s->anchor_x==16&&s->anchor_y==16,"impact frames share32px logical pivot");
    }
    for(unsigned id=846u;id<854u;++id){
        const garden_sprite_t*s=garden_asset(id);
        CHECK(s->width==56u&&s->height==56u&&s->anchor_x==28&&s->anchor_y==28,"blast frames share56px logical pivot");
    }
    for(unsigned id=857u;id<859u;++id){
        const garden_sprite_t*s=garden_asset(id);
        CHECK(s->width==25u&&s->height==120u&&s->anchor_x==12&&s->anchor_y==0,"laser120row tile matches 13px-centered hazard");
    }
    if(dump)CHECK(!fclose(dump),"metadata dump close");
    printf("PASS: ordnance asset API checks=%u descriptor_bytes=%u count=%u atlas_bytes=%u\n",
           checks,(unsigned)sizeof(garden_sprite_t),GARDEN_ASSET_COUNT,GARDEN_ATLAS_BYTES);
    return 0;
}

