"""Host-only comparison of frozen original/candidate Boss cores. No board access."""
from pathlib import Path
import csv, hashlib, json, re, subprocess

HERE=Path(__file__).resolve().parents[1]/'build/host_cadence'
HERE.mkdir(exist_ok=True,parents=True)
VARIANT=Path(__file__).resolve().parents[1]
CC='D:/mingw64/bin/gcc.exe'

HARNESS=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.c"
static unsigned long checks;
#define CHECK(c,m)do{checks++;if(!(c)){fprintf(stderr,"FAIL line%u %s\n",__LINE__,m);exit(2);}}while(0)
static void setup(GardenGame*g,unsigned seed,unsigned band,unsigned attack){
 garden_init(g,seed);g->mode=GARDEN_MODE_BOSS;
 g->boss=(GardenBoss){.active=1,.x=320000,.y=120000,.target_x=320000,
 .hp=band==0?50000:band==1?30000:10000,.max_hp=50000,.attack=attack,.volley_colour=1};
}
static unsigned scale(unsigned x){return SHORTEN?(x*4U+2U)/5U:x;}
static unsigned live(const GardenGame*g){unsigned n=0;for(unsigned i=0;i<garden_enemy_scan_limit;i++)n+=g->enemy_bullets[i].active!=0U;return n;}
static void schedules(void){
 for(unsigned seed=1;seed<=512;seed++)for(unsigned band=0;band<3;band++)for(unsigned attack=1;attack<=3;attack++){
  GardenGame g,r;setup(&g,seed,band,attack);r=g;
  unsigned charge=20U-4U*band+grand(&r,9),rest=80U-16U*band+grand(&r,25);
  unsigned gap=attack==2?6U-band+grand(&r,3):12U-2U*band+grand(&r,5);
  boss_schedule_charge(&g,1);
  CHECK(g.rng==r.rng,"same three PRNG calls/order");CHECK(g.boss.charge_ticks==charge,"warning unchanged");
  CHECK(g.boss.rest_ticks==scale(rest),"rest scaled only");CHECK(g.boss.burst_gap==scale(gap),"burst gap scaled only");
  CHECK(g.boss.burst_gap>0 && g.boss.rest_ticks>0,"timers positive");
  CHECK(g.boss.volley_colour==1+r.rng%5,"same train appearance bank");
  CHECK(g.boss.burst_left==(attack==1?1:attack==2?6:2),"same complete train length");
  CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE,"empty pool begins complete charge");
  CHECK((g.laser.phase==GARDEN_LASER_WARN)==(attack==2),"same attack starts original laser");
 }
}
static void trains(void){
 for(unsigned seed=1;seed<=32;seed++)for(unsigned band=0;band<3;band++)for(unsigned attack=1;attack<=3;attack++){
  GardenGame g;setup(&g,seed,band,attack);boss_schedule_charge(&g,1);
  unsigned pulse=attack==1?64:attack==2?9:36,total=attack==1?64:attack==2?54:72;
  unsigned previous=0,last=0,n=0;
  for(unsigned t=1;t<=1000 && g.boss.rhythm_state!=GARDEN_BOSS_REST;t++){
   run_laser(&g);run_boss(&g);
   unsigned now=g.boss_attack_shots[attack-1];
   if(now!=previous){CHECK(now==previous+1,"one generator per pulse");CHECK(live(&g)==now*pulse,"complete pulse no truncation");
    CHECK(t-last==(previous?g.boss.burst_gap:g.boss.charge_ticks),"actual emission gap equals configured timer");last=t;previous=now;n++;}
   CHECK(live(&g)<=GARDEN_BOSS_BULLET_BUDGET,"live budget never exceeded");
  }
  CHECK(live(&g)==total && n==(attack==1?1:attack==2?6:2),"whole train exact projectile total");
  CHECK(!g.dropped_enemy_bullets && !g.rhythm.shots_deferred,"no drop/truncation");
 }
}
static void waits(void){
 for(unsigned band=0;band<3;band++)for(unsigned attack=1;attack<=3;attack++){
  GardenGame g;setup(&g,91+attack,band,attack);
  for(unsigned i=0;i<80;i++)g.enemy_bullets[i].active=1;
  boss_schedule_charge(&g,1);CHECK(g.boss.rhythm_state==GARDEN_BOSS_WAIT,"full80 pool defers full train");
  unsigned rng=g.rng,charge=g.boss.charge_ticks,rest=g.boss.rest_ticks,gap=g.boss.burst_gap,col=g.boss.volley_colour;
  for(unsigned t=0;t<400;t++){run_laser(&g);run_boss(&g);CHECK(g.rng==rng && g.boss.charge_ticks==charge && g.boss.rest_ticks==rest && g.boss.burst_gap==gap && g.boss.volley_colour==col,"WAIT keeps exact announced values");}
  memset(g.enemy_bullets,0,sizeof g.enemy_bullets);run_boss(&g);
  CHECK(g.boss.rhythm_state==GARDEN_BOSS_CHARGE && g.boss.rhythm_ticks==charge,"capacity recovery repeats full warning");
  CHECK(g.rng==rng,"capacity recovery never redraws timing/type");
 }
 GardenGame g;setup(&g,2,0,2);laser_start(&g);unsigned counts[4]={0};
 for(unsigned t=0;t<100 && g.laser.phase!=GARDEN_LASER_OFF;t++){counts[g.laser.phase]++;run_laser(&g);}
 CHECK(counts[1]==40 && counts[2]==40 && counts[3]==12,"original40/40/12tick laser phases");
}
static void natural(FILE*f){
 for(unsigned seed=1;seed<=8;seed++)for(unsigned band=0;band<3;band++){
  GardenGame g;setup(&g,seed,band,1);boss_schedule_charge(&g,1);
  unsigned last_pulse=0,last_train=0,pulses=0,groups=0,peak=0;unsigned long pulse_sum=0,train_sum=0;
  for(unsigned t=1;t<=8000;t++){
   unsigned old=g.rhythm.shots_admitted,old_groups=g.rhythm.bursts_completed;
   run_laser(&g);run_boss(&g);run_enemy_bullets(&g);update_counts(&g);g.tick++;
   CHECK(g.enemy_bullet_count<=80 && !g.dropped_enemy_bullets,"real lifetimes obey budget no drops");
   if(g.enemy_bullet_count>peak)peak=g.enemy_bullet_count;
   if(g.rhythm.shots_admitted!=old){if(last_pulse){pulse_sum+=t-last_pulse;pulses++;}
    if(seed==1)fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u\n",band,t,g.boss.attack,t-last_pulse,g.enemy_bullet_count,g.rhythm.capacity_wait_ticks,g.boss.rest_ticks,g.boss.burst_gap);last_pulse=t;}
   if(g.rhythm.bursts_completed!=old_groups){if(last_train){train_sum+=t-last_train;groups++;}last_train=t;}
  }
  printf("NATURAL seed=%u band=%u pulses=%u pulse_mean_ticks=%.6f trains=%u train_mean_ticks=%.6f wait_ticks=%u peak=%u\n",seed,band,pulses,pulses?(double)pulse_sum/pulses:0,groups,groups?(double)train_sum/groups:0,g.rhythm.capacity_wait_ticks,peak);
 }
}
int main(int argc,char**argv){CHECK(argc==2,"CSV arg");FILE*f=fopen(argv[1],"w");CHECK(f!=NULL,"CSV opened");fprintf(f,"hp_band,tick,attack,pulse_interval_ticks,live_count,capacity_wait_ticks,rest_ticks,burst_gap\n");schedules();trains();waits();natural(f);fclose(f);printf("PASS checks=%lu shortened=%u\n",checks,SHORTEN);return 0;}
'''

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def stripped(s):return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S))
def main():
 HERE.mkdir(exist_ok=True,parents=True)
 base=VARIANT/'tools/reference_original_core';new=VARIANT/'src'
 inserted=['b->rest_ticks=(uint16_t)((b->rest_ticks*4U+2U)/5U);','b->burst_gap=(uint8_t)((b->burst_gap*4U+2U)/5U);']
 old=base.joinpath('garden_core.c').read_text();candidate=new.joinpath('garden_core.c').read_text()
 reduced=stripped(candidate)
 for line in inserted:
  assert reduced.count(line)==1,line
  reduced=reduced.replace(line,'')
 assert reduced==stripped(old),'Unexpected executable core changes beyond two timer assignments'
 assert base.joinpath('garden_core.h').read_bytes()==new.joinpath('garden_core.h').read_bytes(),'ABI/header changed'
 report={'status':'RUNNING','host_only':True,'board_acceptance':False,'source_boundary':'same tokens except two timer scaling assignments; same header','variants':{}}
 for mode,source,shorten in [('original',base,0),('shortened',new,1)]:
  out=HERE/mode;out.mkdir(exist_ok=True)
  for name in ['garden_core.c','garden_core.h','garden_clear.c','garden_clear.h']:
   (out/('core.c' if name=='garden_core.c' else name)).write_bytes((source/name).read_bytes())
  (out/'harness.c').write_text(HARNESS)
  cmd=[CC,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation',f'-DSHORTEN={shorten}','harness.c','garden_clear.c','-o','test.exe']
  c=subprocess.run(cmd,cwd=out,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr);c.check_returncode()
  r=subprocess.run([str(out/'test.exe'),str(out/'pulse_intervals.csv')],cwd=out,capture_output=True,text=True);(out/'test.log').write_text(r.stdout+r.stderr);r.check_returncode()
  report['variants'][mode]={'source_sha256':sha(source/'garden_core.c'),'log_sha256':sha(out/'test.log'),'csv_sha256':sha(out/'pulse_intervals.csv'),'summary':r.stdout.splitlines()[-1],'natural':r.stdout.splitlines()[:-1]}
 report.update(status='PASS',limits=['Host exercises actual core at40Hz; no FPGA FPS claim.','Natural fixtures run actual Boss/projectile lifetime/culling/laser but omit player collision/auto pilot/damage, hold HP band.','80-live capacity WAIT can dominate final inter-train delay; reduced rest does not promise every train20% faster.'])
 (HERE/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v['summary'] for k,v in report['variants'].items()}));print(HERE/'verification.json')
if __name__=='__main__':main()
