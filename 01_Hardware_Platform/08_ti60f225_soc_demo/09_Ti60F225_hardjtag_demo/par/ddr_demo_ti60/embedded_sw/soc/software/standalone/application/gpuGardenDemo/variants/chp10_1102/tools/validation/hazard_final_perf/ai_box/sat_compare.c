
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
static int32_t abs32(int32_t v){return v<0?-v:v;}
static int64_t abs64(int64_t v){return v<0?-v:v;}
static unsigned segment_box(int32_t ax,int32_t ay,int32_t bx,int32_t by,
                            int32_t x0,int32_t y0,int32_t x1,int32_t y1)
{
    if((ax<x0 && bx<x0)||(ax>x1 && bx>x1)||(ay<y0 && by<y0)||(ay>y1 && by>y1))return 0U;
    if(bx>=x0 && bx<=x1 && by>=y0 && by<=y1)return 1U;
    int64_t mx=(int64_t)ax+bx-x0-x1,my=(int64_t)ay+by-y0-y1;
    int64_t dx=(int64_t)bx-ax,dy=(int64_t)by-ay,hx=(int64_t)x1-x0,hy=(int64_t)y1-y0;
    return abs64(mx*dy-my*dx)<=hx*abs64(dy)+hy*abs64(dx);
}

/* The AI alone tests a fixed square centered at zero. Keep the gameplay
 * collision SAT unchanged. After the same trivial rejections as segment_box,
 * normal forecast coordinates fit +/-10,000,000. Consequently the sums and
 * differences fit +/-20,000,000; each signed 32x32 product has magnitude at
 * most4e14, their difference at most8e14, and the RHS at most1.28e12. No
 * intermediate overflows. Unusual/out-of-range inputs use the original SAT.
 * Unsigned offset checks deliberately avoid signed overflow even at INT_MIN.
 */
static inline __attribute__((always_inline)) unsigned ai_bullet_segment_box(
    int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    if((ax < -16000 && bx < -16000)||(ax > 16000 && bx > 16000)||
       (ay < -16000 && by < -16000)||(ay > 16000 && by > 16000))return 0U;
    if(bx >= -16000 && bx <= 16000 && by >= -16000 && by <= 16000)return 1U;
    if((uint32_t)ax+10000000U>20000000U || (uint32_t)ay+10000000U>20000000U ||
       (uint32_t)bx+10000000U>20000000U || (uint32_t)by+10000000U>20000000U)
        return segment_box(ax,ay,bx,by,-16000,-16000,16000,16000);
    int32_t mx=ax+bx,my=ay+by,dx=bx-ax,dy=by-ay;
    int64_t cross=(int64_t)mx*dy-(int64_t)my*dx;
    int64_t extent=INT64_C(32000)*(abs32(dx)+abs32(dy));
    return abs64(cross)<=extent;
}

static uint32_t rng=0x1738aecbu;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static __int128 abs128(__int128 a){return a<0?-a:a;}
static unsigned oracle(int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    if((ax < -16000 && bx < -16000)||(ax > 16000 && bx > 16000)||
       (ay < -16000 && by < -16000)||(ay > 16000 && by > 16000))return 0U;
    if(bx >= -16000 && bx <= 16000 && by >= -16000 && by <= 16000)return 1U;
    __int128 mx=(__int128)ax+bx,my=(__int128)ay+by,dx=(__int128)bx-ax,dy=(__int128)by-ay;
    return abs128(mx*dy-my*dx)<=32000*(abs128(dx)+abs128(dy));
}
static uint64_t checks,fallback;
static void test(int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    unsigned a=ai_bullet_segment_box(ax,ay,bx,by),b=segment_box(ax,ay,bx,by,-16000,-16000,16000,16000),c=oracle(ax,ay,bx,by);
    checks++;
    if((uint32_t)ax+10000000u>20000000u || (uint32_t)ay+10000000u>20000000u ||
       (uint32_t)bx+10000000u>20000000u || (uint32_t)by+10000000u>20000000u)fallback++;
    if(a!=b||a!=c){fprintf(stderr,"FAIL %d %d %d %d optimized=%u original=%u int128=%u\n",ax,ay,bx,by,a,b,c);exit(2);}
}
int main(void)
{
    static const int32_t v[]={-2000000000,-10000001,-10000000,-9999999,-16001,-16000,-15999,-1,0,1,15999,16000,16001,9999999,10000000,10000001,2000000000};
    for(unsigned a=0;a<17;a++)for(unsigned b=0;b<17;b++)for(unsigned c=0;c<17;c++)for(unsigned d=0;d<17;d++)test(v[a],v[b],v[c],v[d]);
    for(unsigned i=0;i<1000000;i++){
        int32_t a=(int32_t)((int64_t)(next()%4000000001u)-2000000000),b=(int32_t)((int64_t)(next()%4000000001u)-2000000000);
        int32_t c=(int32_t)((int64_t)(next()%4000000001u)-2000000000),d=(int32_t)((int64_t)(next()%4000000001u)-2000000000);
        test(a,b,c,d);
    }
    for(unsigned i=0;i<1000000;i++){
        int32_t a=(int32_t)(next()%20000001u)-10000000,b=(int32_t)(next()%20000001u)-10000000;
        int32_t c=(int32_t)(next()%20000001u)-10000000,d=(int32_t)(next()%20000001u)-10000000;
        test(a,b,c,d);
    }
    printf("PASS SAT comparisons=%llu wide_range_or_boundary_inputs=%llu independent_int128=1\n",(unsigned long long)checks,(unsigned long long)fallback);
}
