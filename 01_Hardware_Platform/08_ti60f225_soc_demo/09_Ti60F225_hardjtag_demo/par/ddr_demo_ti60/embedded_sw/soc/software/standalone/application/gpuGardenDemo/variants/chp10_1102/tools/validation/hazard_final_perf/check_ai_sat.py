"""Independent fixed-square SAT regression; no game or hardware writes."""
from pathlib import Path
import subprocess,re,json
H=Path(__file__).resolve().parent
source=(H/'reference_core.c').read_text()
start=source.index('static unsigned segment_box(')
end=source.index('\n}\n',start)+3
general=source[start:end]
helper=(H/'ai_box_patch.txt').read_text()
c=r'''
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
static int32_t abs32(int32_t v){return v<0?-v:v;}
static int64_t abs64(int64_t v){return v<0?-v:v;}
'''+general+'\n'+helper+r'''
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
'''
p=H/'ai_box/sat_compare.c';p.write_text(c)
for label,flags in [('Os',['-Os']),('O2',['-O2'])]:
    exe=H/('ai_box/sat_compare_'+label+'.exe')
    cmd=['D:/mingw64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror',*flags,str(p),'-o',str(exe)]
    result=subprocess.run(cmd,capture_output=True,text=True);(H/('ai_box/sat_compile_'+label+'.log')).write_text(result.stdout+result.stderr)
    assert not result.returncode,result.stderr
    result=subprocess.run([str(exe)],capture_output=True,text=True);(H/('ai_box/sat_compare_'+label+'.log')).write_text(result.stdout+result.stderr)
    assert not result.returncode,result.stdout+result.stderr
    print(label,result.stdout.strip())
