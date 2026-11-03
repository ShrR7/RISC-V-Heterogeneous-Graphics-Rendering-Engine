/* GPL-3.0-or-later. Uniform two-degree fan with a common tunable radial speed. */
#include "pure_bullet_test.h"
#include "pad_hw.h"
#include "garden_controller.h"
_Static_assert(sizeof(PureBulletParticle)==4U,"particle ABI");
_Static_assert(sizeof(PureBulletDesc)==16U,"descriptor ABI");
static int radial_pixel(int unit,unsigned age,unsigned speed_q8)
{return (unit*(int)(age*speed_q8)+(1<<21))>>22;}
/* k30/cut6 can exceed Stage9's 2048 descriptors. Retain the verified
 * cut7 route at k<=22 and k30; use cut6 for the intervening capacities. */
static unsigned route_cut(unsigned count)
{return count>=23U*PURE_RAY_COUNT && count<=29U*PURE_RAY_COUNT?6U:7U;}
static unsigned edge_age(const PureBulletRayMeta *ray,unsigned speed_q8)
{
    int ux=(int16_t)ray->velocity,uy=(int16_t)(ray->velocity>>16);
    int ax=(int)(ray->packed&255U),ay=(int)((ray->packed>>8)&255U);
    for(unsigned age=1U;age<512U;age++){
        int x=320+radial_pixel(ux,age,speed_q8)-ax;
        int y=16+radial_pixel(uy,age,speed_q8)-ay;
        if(x<0 || y<0 || x+16>640 || y+16>480)return age;
    }
    return 0U;
}
void pure_bullet_test_model_init(PureBulletParticle *state,unsigned count,
    const PureBulletPlan *plans,const PureBulletRayMeta *rays,unsigned speed_q8)
{
    if(!state || !plans || !rays || !count || count>PURE_BULLET_MAX || count%PURE_RAY_COUNT ||
       speed_q8<PURE_SPEED_MIN_Q8 || speed_q8>PURE_SPEED_MAX_Q8)return;
    unsigned per_ray=count/PURE_RAY_COUNT;
    if(per_ray<1U || per_ray>PURE_PER_RAY_MAX)return;
    for(unsigned i=0;i<count;i++){
        unsigned ray=plans[i].ray,slot=plans[i].phase_slot;
        if(ray>=PURE_RAY_COUNT || !slot || slot>per_ray)return;
        unsigned ttl=i<PURE_RAY_COUNT?edge_age(rays+ray,speed_q8):state[ray].ttl;
        if(!ttl || ttl>511U)return;
        state[i]=(PureBulletParticle){(uint16_t)(((slot-1U)*ttl)/per_ray),(uint16_t)ttl};
    }
}
/* Keep the particle builder optimized independently of the size-tuned UI
 * and input code: submission must finish before the physical row480 gate. */
__attribute__((optimize("O3"),noinline))
int pure_bullet_test_model_build(PureBulletParticle *state,
    PureBulletDesc *s9,uint32_t *legacy_destinations,unsigned count,
    uint32_t framebuffer,const PureBulletPlan *plans,const PureBulletRayMeta *rays,
    unsigned speed_q8,
    PureBulletBuildResult *result)
{
    if(!result)return -1;
    *result=(PureBulletBuildResult){0};
    if(!state || !s9 || !legacy_destinations || !plans || !rays || count>PURE_BULLET_MAX ||
       speed_q8<PURE_SPEED_MIN_Q8 || speed_q8>PURE_SPEED_MAX_Q8)return -1;
    uint32_t crc=0x2000C0DEU;
    unsigned cut=route_cut(count);
    for(unsigned i=0;i<count;i++){
        unsigned ray=plans[i].ray;if(ray>=PURE_RAY_COUNT)return -1;
        unsigned packed=rays[ray].packed,tile=(packed>>16)&255U,family=packed>>24;
        int ax=(int)(packed&255U),ay=(int)((packed>>8)&255U);
        if(tile>=27U || family>=7U || ax>15 || ay>15 || !state[i].ttl || state[i].ttl>511U)return -1;
        unsigned old=state[i].age,age=old+1U;
        if(old>=state[i].ttl)return -1;
        if(age>=state[i].ttl){age=0U;result->respawn++;}state[i].age=(uint16_t)age;
        uint32_t unit=rays[ray].velocity;
        int px=radial_pixel((int16_t)unit,age,speed_q8);
        int py=radial_pixel((int16_t)(unit>>16),age,speed_q8);
        /* Q8 speed>=512 means >=2px radial step. At least one Q14 axis is
         * >=0.707, so rounded integer coordinates change every update;
         * natural edge respawn also changes position. */
        result->changed++;
        int lx=320+px-ax,ly=16+py-ay;
        if(lx<0 || ly<0 || lx+16>640 || ly+16>480)return -1;
        int16_t gx=(int16_t)(lx+320),gy=(int16_t)(ly+120);
        /* Lightweight ordered position signature; pixels and routing are
         * independently checked. Avoid per-particle diagnostic multiplies. */
        crc=((crc<<5)|(crc>>27))^((uint32_t)gx|((uint32_t)gy<<16));
        /* RTL19 selects Stage9 only for physical scan lines 0..479.
         * A tile touching row480 must use the GPU framebuffer on both sides
         * of that scanout boundary, including its top rows. */
        if(ray>=cut && ray<PURE_RAY_COUNT-cut && gy+16<=480){
            if(result->s9_count>=2048U)return -3;
            s9[result->s9_count++]=(PureBulletDesc){gx,gy,0,0,(uint16_t)tile,1U,0U};
        }else{
            if(result->legacy_count[tile]>=PURE_SCATTER_BUCKET)return -2;
            unsigned n=tile*PURE_SCATTER_BUCKET+result->legacy_count[tile]++;
            legacy_destinations[n]=framebuffer+(uint32_t)gy*2560U+(uint32_t)gx*2U;
        }
        result->visible++;result->family_count[family]++;
    }
    result->checksum=crc;return 0;
}

