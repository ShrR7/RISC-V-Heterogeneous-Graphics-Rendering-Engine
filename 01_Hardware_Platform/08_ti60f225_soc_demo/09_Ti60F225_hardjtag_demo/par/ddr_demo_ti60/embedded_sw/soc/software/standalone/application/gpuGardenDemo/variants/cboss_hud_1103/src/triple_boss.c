/* SPDX-License-Identifier: GPL-3.0-or-later. Three independent natural-edge emitters.
 * Static bosses are in the immutable framebuffer background. All whole tiles
 * above physical row480 use Stage9; the lower464..599 strip uses the GPU.
 * Staggered source heights and ray birth phases avoid coincident hotspots.
 */
#include "triple_boss.h"
static const int16_t origin_x[3]={320,180,460};
static const int16_t origin_y[3]={79,118,119};
#ifndef PURE_BULLET_HOST_TEST
static const int16_t boss_y[3]={40,80,80};
#endif
static int position(int unit,unsigned age,unsigned speed)
{return (unit*(int)(age*speed)+(1<<21))>>22;}
void triple_boss_init(PureBulletParticle *p,unsigned count,const PureBulletRayMeta *rays,unsigned speed)
{
    if(!p || !rays || count%91U || count<728U || count>TRIPLE_BULLET_MAX)return;
    unsigned waves=count/91U;
    for(unsigned i=0;i<count;i++){
        unsigned wave=i/91U,ray=i%91U,boss=wave%3U,slots=(waves+2U-boss)/3U;
        const PureBulletRayMeta *r=rays+30U+(ray+1U)/3U;
        unsigned ttl=0;
        if(wave>=3U)ttl=p[boss*91U+ray].ttl;
        else for(unsigned a=1;a<512U;a++){
            int x=origin_x[boss]+position((int16_t)r->velocity,a,speed)-(int)(r->packed&255U);
            int y=origin_y[boss]+position((int16_t)(r->velocity>>16),a,speed)-(int)((r->packed>>8)&255U);
            if(x<0 || x+16>640 || y<0 || y+16>480){ttl=a;break;}
        }
        unsigned phase=((ray*2654435761U+boss*2246822507U)>>16)%ttl;
        p[i]=(PureBulletParticle){(uint16_t)(((wave/3U)*ttl/slots+phase)%ttl),(uint16_t)ttl};
    }
}
__attribute__((optimize("O3"),noinline))
int triple_boss_build(PureBulletParticle *p,PureBulletDesc *desc,uint32_t *legacy,unsigned count,
    uint32_t fb,const PureBulletRayMeta *rays,unsigned speed,PureBulletBuildResult *out)
{
    if(!out)return -1;
    *out=(PureBulletBuildResult){0};
    if(!p || !desc || !legacy || !rays || count>TRIPLE_BULLET_MAX || count%91U)return -1;
    uint32_t hash=0x33424F53U;
    for(unsigned wave=0;wave<count/91U;wave++){
      unsigned boss=wave%3U;
      for(unsigned ray=0;ray<91U;ray++){
        const PureBulletRayMeta *r=rays+30U+(ray+1U)/3U;
        unsigned i=wave*91U+ray,packed=r->packed;
        unsigned tile=(packed>>16)&255U,family=packed>>24,age=p[i].age+1U;
        if(tile>=27U || family>=7U || !p[i].ttl || p[i].age>=p[i].ttl)return -1;
        if(age>=p[i].ttl){age=0;out->respawn++;}p[i].age=(uint16_t)age;
        int x=origin_x[boss]+position((int16_t)r->velocity,age,speed)-(int)(packed&255U);
        int y=origin_y[boss]+position((int16_t)(r->velocity>>16),age,speed)-(int)((packed>>8)&255U);
        if(x<0 || x+16>640 || y<0 || y+16>480)return -1;
        x+=320;y+=120;
        if(y+16<=480){
            if(out->s9_count>=2048U)return -3;
            desc[out->s9_count++]=(PureBulletDesc){(int16_t)x,(int16_t)y,0,0,(uint16_t)tile,1U,0U};
        }else{
            unsigned n=out->legacy_count[tile]++;if(n>=256U)return -2;
            legacy[tile*256U+n]=fb+(uint32_t)y*2560U+(uint32_t)x*2U;
        }
        out->visible++;out->changed++;out->family_count[family]++;
        hash=((hash<<5)|(hash>>27))^((uint32_t)x|((uint32_t)y<<16));
      }
    }
    out->checksum=hash;return 0;
}
#ifndef PURE_BULLET_HOST_TEST
#include "garden_assets.h"
#include "gpu_driver.h"
#include "gpu_display.h"
int triple_boss_backdrop(void)
{
    for(unsigned i=0;i<3U;i++){
        const garden_sprite_t *s=garden_asset(GARDEN_ASSET_TRIPLE_BOSS0+i);
        if(!s)return GPU_EINVAL;
        unsigned x=320U+(unsigned)origin_x[i]-(unsigned)s->anchor_x;
        unsigned y=120U+(unsigned)boss_y[i]-(unsigned)s->anchor_y;
        int rc=gpu_bitblt(GARDEN_ATLAS_ADDRESS+s->offset,display_back_addr()+y*2560U+x*2U,
            s->width,s->height,s->width*2U,2560U,GARDEN_KEY_COLOR);
        if(rc)return rc;
    }
    return 0;
}
#endif
