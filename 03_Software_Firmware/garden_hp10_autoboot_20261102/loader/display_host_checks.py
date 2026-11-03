"""Execute the actual display-init source block with mocked MMIO/time."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent
source=(HERE/'stage2.c').read_text()
block=source.split('    /* Replace the accepted host-side display initialization as well as downloads. */\n',1)[1]
block=block.split('    /* The validator checks every range before the first low-DDR overwrite. */',1)[0]
block=block.replace('__asm__ volatile("fence iorw,iorw":::"memory");','mock_fence();')
out=HERE/'build/display_host';out.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#define HZ 100000000U
#define MTIME_LOW 0xF8B0BFF8U
static uint32_t timer, elapsed, id,ctrl,ctrl_bad,qvalue,ctrl_access,timer_reads;
static unsigned fences,pass_messages,checks;
static uint32_t trapped_code,trapped_detail;
static jmp_buf escape;
static void check(int ok){checks++;if(!ok){fprintf(stderr,"check%u failed\n",checks);exit(1);}}
static uint32_t *mock_ptr(uint32_t addr){
 if(addr==MTIME_LOW){timer+=1000000U;elapsed+=1000000U;timer_reads++;return &timer;}
 if(addr==0xF810006CU)return &id;
 if(addr==0xF810005CU){ctrl_access++;return ctrl_access==1U?&ctrl:(ctrl_bad?&ctrl_bad:&ctrl);}
 if(addr==0xF8100074U)return &qvalue;
 check(0);return 0;
}
#define REG(a) (*mock_ptr(a))
static void mock_fence(void){fences++;}
static void text(const char *s){if(s[0]=='[')pass_messages++;}
static void fatal(uint32_t code,uint32_t detail){trapped_code=code;trapped_detail=detail;longjmp(escape,1);}
static void display_init(void){
'''
suffix=r'''
}
static void run(unsigned variant,uint32_t start){
 timer=start;elapsed=0;id=0x44424731U;ctrl=0;ctrl_bad=0;qvalue=0x400U;
 ctrl_access=timer_reads=fences=pass_messages=trapped_code=trapped_detail=0;
 if(variant==1)id=0;
 if(variant==2)ctrl_bad=2;
 if(variant==3)qvalue=0;
 if(setjmp(escape)==0){display_init();check(variant==0);}
 if(variant==0){check(trapped_code==0);check(ctrl==4);check(fences==1);check(pass_messages==1);check(timer_reads>=5);check(elapsed-1000000U>=4000000U);}
 if(variant==1){check(trapped_code==0x106U);check(trapped_detail==0);check(ctrl_access==0);check(timer_reads==0);check(pass_messages==0);}
 if(variant==2){check(trapped_code==0x107U);check(trapped_detail==2);check(ctrl==4);check(pass_messages==0);check(elapsed-1000000U>=4000000U);}
 if(variant==3){check(trapped_code==0x108U);check(trapped_detail==0);check(ctrl==4);check(pass_messages==0);check(elapsed-1000000U>=4000000U);}
}
int main(void){for(unsigned v=0;v<4;v++){run(v,0);run(v,0xFFE00000U);}printf("PASS %u\n",checks);return 0;}
'''
(out/'display_host.c').write_text(prefix+block+suffix)
cc=shutil.which('gcc') or 'D:/mingw64/bin/gcc.exe'
subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(out/'display_host.c'),'-o',str(out/'display_host.exe')],check=True)
r=subprocess.run([str(out/'display_host.exe')],text=True,capture_output=True,check=True)
result={'status':'PASS','checks':int(r.stdout.strip().split()[-1]),
        'source':'actual stage2.c display-init block extracted unchanged except hardware fence callback',
        'coverage':['40ms minimum wait','32-bit timer wrap','DBG_ID invalid','CTRL readback invalid','relock bit missing','success message only after verified'],
        'stage2_c_sha256':hashlib.sha256(source.encode()).hexdigest(),
        'firmware_source_modified_by_test':False}
(HERE/'display_host_results.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
