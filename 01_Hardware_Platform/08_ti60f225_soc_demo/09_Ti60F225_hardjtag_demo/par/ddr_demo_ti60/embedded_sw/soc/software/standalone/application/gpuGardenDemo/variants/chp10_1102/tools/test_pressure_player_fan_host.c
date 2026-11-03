/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual production60degree fan plus per-buffer RGB565 player restoration.
 * Objects/motion are production model; GPU/display/input remain host mocks. */
#define main old_player_test_main
#include "test_pressure_player_host.c"
#undef main
#include "triple_boss.h"
#include <math.h>
static PureBulletRayMeta rays[91];static PureBulletParticle particles[2730];
static PureBulletDesc desc[2048];static uint32_t legacy_list[27*256];
static uint16_t expected[720*1280];
static int at_edge(unsigned i,unsigned age)
{
 static const int xs[3]={320,180,460},ys[3]={79,118,119};
 unsigned boss=(i/91U)%3U,ray=30U+(i%91U+1U)/3U;
 const PureBulletRayMeta *r=&rays[ray];
 int x=xs[boss]+(int)(((int64_t)(int16_t)r->velocity*age*612+(1<<21))>>22)-(int)(r->packed&255U);
 int y=ys[boss]+(int)(((int64_t)(int16_t)(r->velocity>>16)*age*612+(1<<21))>>22)-(int)((r->packed>>8)&255U);
 return x<0||y<0||x+16>640||y+16>480;
}
int main(int argc,char**argv){
 if(argc!=3){fprintf(stderr,"usage: %s scatter.bin atlas.bin\n",argv[0]);return 2;}
 FILE*f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,PURE_SCATTER_RAY_OFFSET,SEEK_SET);CHECK(fread(rays,sizeof rays,1,f)==1);fclose(f);
 char *player_args[]={argv[0],argv[2]};old_player_test_main(2,player_args);
 for(unsigned ray=0;ray<91;ray++){
  unsigned r=30+(ray+1)/3;CHECK(r>=30&&r<=60);int16_t vx=rays[r].velocity,vy=rays[r].velocity>>16;
  CHECK(vy>0&&fabs((double)vx/(double)vy)<0.578);
 }
 unsigned counts[]={1911,2002,2093,2275,2366};
 for(unsigned j=0;j<5;j++){
  unsigned count=counts[j],s9max=0,gpumax=0;triple_boss_init(particles,count,rays,612);
  for(unsigned i=0;i<count;i++){
   CHECK(particles[i].ttl>0U && particles[i].age<particles[i].ttl);
   CHECK(!at_edge(i,particles[i].ttl-1U) && at_edge(i,particles[i].ttl));
  }
  for(unsigned frame=0;frame<4096;frame++){
   PureBulletBuildResult out;CHECK(!triple_boss_build(particles,desc,legacy_list,count,0x200000,rays,612,&out));
   CHECK(out.visible==count&&out.changed==count);
   unsigned gpu=0;for(unsigned t=0;t<27;t++){CHECK(out.legacy_count[t]<=256);gpu+=out.legacy_count[t];}
   CHECK(gpu+out.s9_count==count&&out.s9_count<=2048);
   for(unsigned i=0;i<out.s9_count;i++)CHECK(desc[i].x>=320&&desc[i].x+16<=960&&desc[i].y>=120&&desc[i].y+16<=480);
   if(gpu>gpumax)gpumax=gpu;if(out.s9_count>s9max)s9max=out.s9_count;
  }
  printf("FAN count=%u frames=4096 s9max=%u gpumax=%u cone=60..120 natural_edge=1\n",count,s9max,gpumax);
 }
 CHECK(!pressure_player_init());triple_boss_init(particles,2093,rays,612);held=GARDEN_INPUT_UP|GARDEN_INPUT_LEFT;
 for(unsigned frame=0;frame<320;frame++){
  back=frame&1;uint32_t fb=display_back_addr();CHECK(!pressure_player_restore(fb,back));
  memcpy(pixels[back]+464*1280,baseline[back]+464*1280,136*1280*2); /* real clear region, with deterministic restored background */
  memcpy(expected,baseline[back],sizeof expected);
  PureBulletBuildResult out;CHECK(!triple_boss_build(particles,desc,legacy_list,2093,fb,rays,612,&out));
  for(unsigned t=0;t<27;t++)for(unsigned i=0;i<out.legacy_count[t];i++){
   uint32_t a=legacy_list[t*256+i];unsigned off=(a-fb)/2;CHECK(off/1280>=464&&off/1280+16<=600);
   for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){pointer(a+y*2560)[x]=(uint16_t)(0x1000+t);expected[off+y*1280+x]=(uint16_t)(0x1000+t);}
  }
  if(frame==200)held=GARDEN_INPUT_RIGHT|GARDEN_INPUT_SLOW;
  CHECK(!pressure_player_draw(fb,back,30000+frame*50));
  unsigned off=(destination-fb)/2;
  for(unsigned y=0;y<sprite.height;y++)for(unsigned x=0;x<sprite.width;x++)if(art[y*sprite.width+x]!=GARDEN_KEY_COLOR)expected[off+y*1280+x]=art[y*sprite.width+x];
  CHECK(!memcmp(expected,pixels[back],sizeof expected));
 }
 for(back=0;back<2;back++){CHECK(!pressure_player_restore(display_back_addr(),back));memcpy(pixels[back]+464*1280,baseline[back]+464*1280,136*1280*2);restored();}
 printf("PASS real60-degree fan + player restore/clear/legacy/draw order: %u total assertions,320 RGB565 frames including crossing Boss background\n",checks);return 0;
}
