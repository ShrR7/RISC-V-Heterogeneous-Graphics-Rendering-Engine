from pathlib import Path
import subprocess,shutil
V=Path(__file__).resolve().parents[1]
O=V/'build/host/triple_model';O.mkdir(parents=True,exist_ok=True)
code=r'''#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "triple_boss.h"
static PureBulletParticle p[2730];static PureBulletDesc d[2048];static uint32_t legacy[27*256];
int main(void){PureBulletRayMeta rays[91];FILE*f=fopen("scatter.bin","rb");assert(f);fseek(f,PURE_SCATTER_RAY_OFFSET,SEEK_SET);assert(fread(rays,sizeof(rays),1,f)==1);fclose(f);
 unsigned speeds[3]={512,612,1024};unsigned frames=0;
 for(unsigned k=0;k<3;k++)for(unsigned n=728;n<=2730;n+=91){
  triple_boss_init(p,n,rays,speeds[k]);unsigned max_s9=0,max_legacy=0,overflow=0;
  for(unsigned t=0;t<500;t++){
   PureBulletBuildResult b;int rc=triple_boss_build(p,d,legacy,n,0x100000,rays,speeds[k],&b);
   if(rc==-3){overflow=1;break;}assert(rc==0);unsigned total=b.s9_count,l=0;
   assert(b.visible==n && b.changed==n);
   for(unsigned i=0;i<b.s9_count;i++)assert(d[i].x>=320 && d[i].x+16<=960 && d[i].y>=120 && d[i].y+16<=480 && d[i].sprite<27);
   for(unsigned tile=0;tile<27;tile++)for(unsigned i=0;i<b.legacy_count[tile];i++){
    unsigned a=(legacy[tile*256+i]-0x100000)/2,y=a/1280,x=a%1280;
    assert(x>=320 && x+16<=960 && (y>=465 && y+16<=600));total++;l++;
   }
   assert(total==n);if(b.s9_count>max_s9)max_s9=b.s9_count;if(l>max_legacy)max_legacy=l;frames++;
  }
  printf("speed=%u count=%u max_s9=%u max_legacy=%u bounded_overflow=%u\n",speeds[k],n,max_s9,max_legacy,overflow);
 }
 printf("PASS actual triple model bounds/natural respawn/count/routing %u frames; no board performance claim\n",frames);
}'''
(O/'test.c').write_text(code);shutil.copyfile(V/'assets/pure_scatter16.bin',O/'scatter.bin')
subprocess.run(['D:/mingw64/bin/gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-DPURE_BULLET_HOST_TEST','-I'+str(V/'src'),str(O/'test.c'),str(V/'src/triple_boss.c'),'-o',str(O/'test.exe')],check=True)
r=subprocess.run([str(O/'test.exe')],cwd=O,capture_output=True,text=True);(O/'result.log').write_text(r.stdout+r.stderr);print(r.stdout[-2700:]+r.stderr);r.check_returncode()
