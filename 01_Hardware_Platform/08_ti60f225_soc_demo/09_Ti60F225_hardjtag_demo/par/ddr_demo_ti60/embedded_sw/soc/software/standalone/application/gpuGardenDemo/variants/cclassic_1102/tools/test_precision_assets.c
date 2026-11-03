/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "garden_assets.h"
static unsigned checks;
#define CHECK(c,m) do {checks++;if(!(c)){fprintf(stderr,"FAIL line%u:%s\n",(unsigned)__LINE__,m);exit(2);}}while(0)
int main(void)
{
    CHECK(sizeof(garden_sprite_t)==16U,"descriptor storage estimate matches actual host struct layout");
    CHECK(GARDEN_BULLET_ROTATIONS==8U && GARDEN_PRECISION_ROTATIONS==64U && GARDEN_ASSET_COUNT==840U,"old macro retained and exact precision counts published");
    unsigned used[840]={0};
    for(unsigned type=1;type<=2;type++)for(unsigned colour=1;colour<=5;colour++) {
        unsigned ids[64],next=280U+((type-1U)*5U+colour-1U)*56U;
        for(unsigned bin=0;bin<64;bin++) {
            ids[bin]=bin%8U ? next++ :(type==2U ?52U:12U)+(colour-1U)*8U+bin/8U;
            unsigned got=garden_bullet_precision_id(type,colour,bin*16U);
            CHECK(got==ids[bin] && got<840U && !used[got],"all640type/colour/fine-direction combinations have correct unique ID and legacy slots");used[got]=1;
            const garden_sprite_t *s=garden_asset(got);CHECK(s && s->width && s->height && s->stride==s->width*2U && s->offset+(s->height-1U)*s->stride+s->width*2U<=GARDEN_ATLAS_BYTES,"all640original/fine selected sources valid inside exact atlas");
        }
        for(unsigned angle=0;angle<1024;angle++) {
            unsigned best=0,best_distance=2048;
            /*Independent circular distance search; equal-distance ties select
             * the clockwise center (including1023->0wrap). */
            for(unsigned bin=0;bin<64;bin++) {
                unsigned forward=(bin*16U+1024U-angle)%1024U;
                unsigned distance=forward<=512U ?forward:1024U-forward;
                if(distance<best_distance || (distance==best_distance && forward<=512U)){best=bin;best_distance=distance;}
            }
            CHECK(garden_bullet_precision_id(type,colour,angle)==ids[best],"every1024core-angle sample rounds to independent closest64center with correct ties");
            unsigned legacy=((angle+64U)&1023U)/128U;
            CHECK(garden_bullet_id(type,colour,angle)==(type==2U ?52U:12U)+(colour-1U)*8U+legacy,"old8direction API and static benchmark formula stay exact");
        }
    }
    for(unsigned id=0;id<840;id++)CHECK(garden_asset_is_enemy_bullet(id)==((id>=12U&&id<92U)||id>=280U),"enemy classification includes only old80plusnew560, not fonts/body/markers");
    CHECK(!garden_asset_is_enemy_bullet(840U)&&!garden_asset_is_enemy_bullet(UINT_MAX)&&!garden_asset_is_enemy_bullet(0x2000U)&&!garden_asset(840U),"bounds/virtualassets are not classified as projectile sprites");
    CHECK(garden_bullet_precision_id(1,0,8)==garden_bullet_precision_id(1,3,8) && garden_bullet_precision_id(2,6,8)==garden_bullet_precision_id(2,3,8),"invalid colours retain original fallback family3");
    CHECK(garden_bullet_precision_id(1,1,UINT_MAX)==garden_bullet_precision_id(1,1,1023U),"unsigned angular wrap is safe");
    printf("PASS: precision-asset production API checks=%u descriptor_bytes=%u count=%u atlas_bytes=%u\n",checks,(unsigned)sizeof(garden_sprite_t),GARDEN_ASSET_COUNT,GARDEN_ATLAS_BYTES);return 0;
}
