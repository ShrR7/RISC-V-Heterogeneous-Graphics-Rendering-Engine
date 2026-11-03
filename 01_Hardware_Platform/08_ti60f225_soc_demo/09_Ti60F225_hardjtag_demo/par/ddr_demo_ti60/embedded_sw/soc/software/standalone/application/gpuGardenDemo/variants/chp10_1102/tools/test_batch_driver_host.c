
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GPU_DRIVER_BATCH_HOST_TEST 1
#include "gpu_driver.c"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint32_t regs[64], wo[32], wv[32], wn, reads, starts;
static uint32_t before_status, after_status[8], after_n, after_i;
static uint32_t fences, invalidates;
static uint32_t firstwrite_fences, firstwrite_invalidates;
static unsigned char saved[8192];
void gpu_batch_test_write(uint32_t off,uint32_t val) {
    CHECK(wn < 32U);
    if (wn==0U) { firstwrite_fences=fences;firstwrite_invalidates=invalidates; }
    wo[wn]=off;wv[wn++]=val;regs[off/4U]=val;
    if (off==0U && (val&1U)) { starts++;after_i=0U; }
}
uint32_t gpu_batch_test_read(uint32_t off) {
    CHECK(off==4U);reads++;
    if (!starts) return before_status;
    if (after_i+1U < after_n) return after_status[after_i++];
    return after_status[after_n-1U];
}
void gpu_batch_test_fence(void) { fences++; }
void gpu_batch_test_invalidate(void) { invalidates++; }
static void reset(void) {
    memset(regs,0,sizeof regs);wn=reads=starts=fences=invalidates=0;
    firstwrite_fences=firstwrite_invalidates=0;
    before_status=0;after_n=1;after_i=0;after_status[0]=STATUS_DONE;
}
static void no_traffic(void) { CHECK(wn==0U);CHECK(reads==0U);CHECK(fences==0U);CHECK(invalidates==0U); }
static void passed_ctrl(uint32_t ctrl) {
    CHECK(starts==1U);CHECK(wn>=2U);CHECK(wo[wn-2U]==0U);CHECK(wv[wn-2U]==0U);
    CHECK(wo[wn-1U]==0U);CHECK(wv[wn-1U]==ctrl);
    CHECK(firstwrite_fences==2U);CHECK(firstwrite_invalidates==1U);
    CHECK(fences==4U);CHECK(invalidates==2U);
    CHECK(gpu_last_status()==STATUS_DONE);
}
int main(void) {
    void *area=VirtualAlloc((void *)(uintptr_t)0x01000000U,16384U,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(area==(void *)(uintptr_t)0x01000000U);
    GpuFillEntry *f=(GpuFillEntry *)area;
    GpuBlitEntry *b=(GpuBlitEntry *)area;
    CHECK(sizeof *f==8U);CHECK(sizeof *b==4U);
    for(uint32_t i=0;i<1024U;i++) { f[i].dst=0x00200000U+i*64U;f[i].width=8;f[i].height=2; }
    memcpy(saved,f,sizeof saved);
    reset();CHECK(gpu_fill_batch(f,1024U,2560U,0x1234U)==GPU_OK);passed_ctrl(0x201U);
    CHECK(regs[0x34/4]==0x01000000U);CHECK(regs[0x38/4]==1024U);
    CHECK(regs[0x1c/4]==2560U);CHECK(regs[0x20/4]==0x1234U);
    CHECK(memcmp(saved,f,sizeof saved)==0);
    reset();CHECK(gpu_fill_batch(NULL,0,0,0)==GPU_OK);no_traffic();
    reset();CHECK(gpu_fill_batch(f,1025,2560,0)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_fill_batch(f,65537,2560,0)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_fill_batch((const GpuFillEntry *)((uintptr_t)f+8),1,2560,0)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_fill_batch((const GpuFillEntry *)(uintptr_t)0x1000,1,2560,0)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_fill_batch((const GpuFillEntry *)(uintptr_t)0x08000000,1,2560,0)==GPU_EINVAL);no_traffic();
    if(sizeof(uintptr_t)>4U) {
        reset();CHECK(gpu_fill_batch((const GpuFillEntry *)((uintptr_t)0x100000000ULL+0x01000000U),1,2560,0)==GPU_EINVAL);no_traffic();
    }
    f[4].dst|=1;reset();CHECK(gpu_fill_batch(f,5,2560,0)==GPU_EINVAL);no_traffic();f[4].dst&=~1U;
    f[4].dst=0xfffffffeU;reset();CHECK(gpu_fill_batch(f,5,2560,0)==GPU_EINVAL);no_traffic();f[4].dst=0x00200100U;
    f[4].dst=0x01000000U;reset();CHECK(gpu_fill_batch(f,5,2560,0)==GPU_EOVERLAP);no_traffic();f[4].dst=0x00200100U;
    f[0].width=0;reset();CHECK(gpu_fill_batch(f,1,2560,0)==GPU_OK);passed_ctrl(0x201);f[0].width=8;
    reset();CHECK(gpu_fill_batch(f,1,15,0)==GPU_EINVAL);no_traffic();
    for(uint32_t i=0;i<1024U;i++) b[i].dst=0x00200000U+i*32U;
    memcpy(saved,b,4096);
    for(uint32_t a=0;a<=32;a++) {
        reset();CHECK(gpu_bitblt_batch(b,9,0x005c4000,8,8,16,2560,0xf81f,a)==GPU_OK);
        passed_ctrl(a==32U?0x205:0x305);CHECK(regs[0x30/4]==a);
        CHECK(regs[0x38/4]==9U);CHECK(memcmp(saved,b,4096)==0);
    }
    reset();CHECK(gpu_bitblt_batch(b,9,0x005c4000,8,8,16,2560,0,33)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_bitblt_batch(b,9,0x005c4001,8,8,16,2560,0,16)==GPU_EINVAL);no_traffic();
    b[8].dst=0x005c4000;reset();CHECK(gpu_bitblt_batch(b,9,0x005c4000,8,8,16,2560,0,16)==GPU_EOVERLAP);no_traffic();b[8].dst=0x00200100;
    b[8].dst=0x01000000;reset();CHECK(gpu_bitblt_batch(b,9,0x005c4000,8,8,16,2560,0,16)==GPU_EOVERLAP);no_traffic();b[8].dst=0x00200100;
    reset();CHECK(gpu_bitblt_batch(b,9,0x01000000,8,8,16,2560,0,16)==GPU_EOVERLAP);no_traffic();
    reset();CHECK(gpu_bitblt_batch(b,9,0x005c4000,2048,8,4098,4096,0,16)==GPU_EINVAL);no_traffic();
    b[0].dst=0x00200000;reset();CHECK(gpu_bitblt_batch(b,1,0x005c4000,2048,8,4096,4098,0,16)==GPU_EINVAL);no_traffic();
    reset();CHECK(gpu_bitblt_batch(b,1,0x005c4000,2048,8,4096,4096,0,32)==GPU_OK);passed_ctrl(0x205);
    reset();before_status=STATUS_BUSY;CHECK(gpu_fill_batch(f,1,2560,0)==GPU_ETIMEOUT);
    CHECK(wn==0U);CHECK(reads==GPU_POLL_MAX);CHECK(fences==0U);CHECK(gpu_last_polls()==GPU_POLL_MAX);
    reset();before_status=STATUS_ERROR|STATUS_DONE;CHECK(gpu_fill_batch(f,1,2560,0)==GPU_EHW);CHECK(wn==0U);CHECK(invalidates==0U);
    reset();after_n=4;after_status[0]=STATUS_BUSY;after_status[1]=STATUS_BUSY|STATUS_ERROR;
    after_status[2]=STATUS_BUSY|STATUS_ERROR;after_status[3]=STATUS_DONE|STATUS_ERROR;
    CHECK(gpu_bitblt_batch(b,1,0x005c4000,8,8,16,2560,0,16)==GPU_EHW);
    CHECK(starts==1U);CHECK(after_i==3U);CHECK(gpu_last_polls()==3U);CHECK(invalidates==2U);
    reset();after_status[0]=STATUS_BUSY|STATUS_ERROR;
    CHECK(gpu_bitblt_batch(b,1,0x005c4000,8,8,16,2560,0,16)==GPU_ETIMEOUT);
    CHECK(starts==1U);CHECK(gpu_last_status()==(STATUS_BUSY|STATUS_ERROR));CHECK(invalidates==1U);
    reset();after_status[0]=0;
    CHECK(gpu_fill_batch(f,1,2560,0)==GPU_ETIMEOUT);CHECK(starts==1U);CHECK(invalidates==1U);
    CHECK(VirtualFree(area,0,MEM_RELEASE));
    printf("PASS batch_driver checks=%u entries=1024 alphas=33 error_drain=1 no_replay=1\n",checks);
    return 0;
}
