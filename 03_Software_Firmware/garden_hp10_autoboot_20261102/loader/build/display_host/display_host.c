
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
    if(REG(0xF810006CU)!=0x44424731U)fatal(0x106U,REG(0xF810006CU));
    REG(0xF810005CU)=4U;
    mock_fence();
    uint32_t display_start=REG(MTIME_LOW);
    while((uint32_t)(REG(MTIME_LOW)-display_start)<HZ*40U/1000U){}
    uint32_t display_ctrl=REG(0xF810005CU),display_q=REG(0xF8100074U);
    if(display_ctrl!=4U)fatal(0x107U,display_ctrl);
    if(!(display_q&0x400U))fatal(0x108U,display_q);
    text("[BOOT] display relock PASS\r\n");

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
