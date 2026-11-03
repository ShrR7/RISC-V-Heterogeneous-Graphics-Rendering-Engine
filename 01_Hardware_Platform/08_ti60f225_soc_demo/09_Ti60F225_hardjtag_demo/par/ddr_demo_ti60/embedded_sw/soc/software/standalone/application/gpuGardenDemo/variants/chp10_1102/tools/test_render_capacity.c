/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-only direct exercise of the real production static command queues.
 * No firmware seam: include the production C translation unit in this fixture.
 * Full-frame tests remain separately compiled production renderer comparisons.
 */
#define main inherited_frame_contract_main
#include "test_batch_render.c"
#undef main
#include "../src/garden_render.c"
static void capacity_init(void)
{
    fake_busy=0;reset(2);
    for(sel=0;sel<2;sel++)CHECK(garden_render_frame(NULL,0)==GPU_OK,"capacity fixture starts with initialized empty buffers");
    sel=0;memset((void *)(uintptr_t)LIST_ADDR,0xA5,2048U);batch_scope=1;
}
static void fill_capacity(unsigned deferred)
{
    capacity_init();uint8_t before[2048];memcpy(before,(void *)(uintptr_t)LIST_ADDR,sizeof(before));
    uint32_t dst=display_back_addr()+100U*DISPLAY_STRIDE_BYTES+100U*2U;
    gpu_fill_submit(dst,3U,2U,0x1234);
    CHECK(batch.count==1U,"first Fill remains queued");
    if(deferred)CHECK(!memcmp(before,(void *)(uintptr_t)LIST_ADDR,sizeof(before)),"lazy first Fill performs no DDR list store");
    gpu_fill_submit(dst+20U,7U,3U,0x1234);
    const GpuFillEntry *list=(const GpuFillEntry *)(uintptr_t)LIST_ADDR;
    CHECK(batch.count==2U && list[0].dst==dst && list[0].width==3U && list[0].height==2U && list[1].dst==dst+20U && list[1].width==7U && list[1].height==3U,"second Fill materializes first variable geometry and second entry in order");
    for(unsigned i=2;i<256U;i++)gpu_fill_submit(dst+(i%32U)*2U,1U,1U,0x1234);
    CHECK(batch.count==256U,"real production Fill queue reaches full 256-entry capacity");
    fail_fill=1;fill_failed_count=0;
    gpu_fill_submit(dst+100U,1U,1U,0x1234);
    CHECK(fill_failed_count==256U && frame_fault && fake_busy && batch.count==0U,"257th Fill triggers full list timeout and consumes queue once");
    CHECK(!memcmp(failed_fill_list,(void *)(uintptr_t)LIST_ADDR,sizeof(failed_fill_list)),"capacity timeout does not write 257th Fill or reuse any list byte");
    unsigned calls=gpu_calls;CHECK(garden_render_frame(NULL,0)==GPU_EHW && gpu_calls==calls,"next frame blocked until timed-out full Fill is idle");
    fake_busy=0;batch_scope=0;garden_render_init();
}
static void blit_capacity(unsigned deferred)
{
    capacity_init();uint8_t before[2048];memcpy(before,(void *)(uintptr_t)LIST_ADDR,sizeof(before));
    const garden_sprite_t *a=garden_asset(GARDEN_ASSET_SHOT);
    uint32_t src=ASSET_BASE_ADDR+a->offset,dst=display_back_addr()+200U*DISPLAY_STRIDE_BYTES+200U*2U;
    gpu_blit_submit(src,dst,a->width,a->height,garden_sprite_stride(a),16U);
    CHECK(batch.count==1U,"first Blit remains queued");
    if(deferred)CHECK(!memcmp(before,(void *)(uintptr_t)LIST_ADDR,sizeof(before)),"lazy first Blit performs no DDR list store");
    gpu_blit_submit(src,dst+30U,a->width,a->height,garden_sprite_stride(a),16U);
    const GpuBlitEntry *list=(const GpuBlitEntry *)(uintptr_t)LIST_ADDR;
    CHECK(batch.count==2U && list[0].dst==dst && list[1].dst==dst+30U,"second Blit materializes ordered first and second destinations");
    for(unsigned i=2;i<256U;i++)gpu_blit_submit(src,dst+(i%32U)*2U,a->width,a->height,garden_sprite_stride(a),16U);
    CHECK(batch.count==256U,"real production Blit queue reaches full 256-entry capacity");
    fail_batch=2;blit_failed_count=0;
    gpu_blit_submit(src,dst+100U,a->width,a->height,garden_sprite_stride(a),16U);
    CHECK(blit_failed_count==256U && frame_fault && fake_busy && batch.count==0U,"257th Blit triggers full list timeout and consumes queue once");
    CHECK(!memcmp(failed_blit_list,(void *)(uintptr_t)LIST_ADDR,sizeof(failed_blit_list)),"capacity timeout does not write 257th Blit or reuse any list byte");
    unsigned calls=gpu_calls;CHECK(garden_render_frame(NULL,0)==GPU_EHW && gpu_calls==calls,"next frame blocked until timed-out full Blit is idle");
    fake_busy=0;batch_scope=0;garden_render_init();
}
int main(int argc,char **argv)
{
    CHECK(argc==3,"actual atlas and expected deferred flag required");
    unsigned deferred=(unsigned)atoi(argv[2]);CHECK(deferred<=1U,"deferred flag is explicit 0/1");
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"capacity fixture host RAM mapping only");
    FILE *f=fopen(argv[1],"rb");CHECK(f && fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,f)==GARDEN_ATLAS_BYTES && fgetc(f)==EOF,"capacity fixture uses actual frozen atlas");fclose(f);
    fill_capacity(deferred);blit_capacity(deferred);
    printf("PASS render_capacity checks=%u fill_capacity=256 blit_capacity=256 overflow_trigger=257 deferred=%u timeout_no_rewrite=1\n",checks,deferred);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
