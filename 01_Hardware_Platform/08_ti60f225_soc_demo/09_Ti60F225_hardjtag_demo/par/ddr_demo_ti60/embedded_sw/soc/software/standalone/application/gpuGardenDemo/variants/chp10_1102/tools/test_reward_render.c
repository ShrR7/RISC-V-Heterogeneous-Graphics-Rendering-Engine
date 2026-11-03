/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the production renderer against the existing independent full
 * sprite oracle. Fixed-address host RAM replaces the FPGA only in this test.
 */
#define main reward_base_main
#include "test_render_batch.c"
#undef main

static uint64_t reward_digest=1469598103934665603ULL;
static unsigned reward_frames,reward_full_bar_commands,reward_zero_commands;
static uint64_t reward_blit_pixels,reward_fill_pixels;

static void reward_reset(unsigned cpu,unsigned alpha_on)
{
    garden_render_init();garden_render_alpha_support(1);garden_render_mode((int)cpu);
    if(!alpha_on) CHECK(garden_render_alpha_toggle()==GPU_OK,"Alpha-off fallback available");
    sel=0U;CHECK(garden_render_frame(NULL,0)==GPU_OK,"first physical FB init");
    sel=1U;CHECK(garden_render_frame(NULL,0)==GPU_OK,"second physical FB init");
    memcpy(base,fb(0),sizeof(base));
}

static void reward_compare(GardenDraw d,unsigned alpha_on)
{
    sel=reward_frames&1U;
    memcpy(expected,base,sizeof(expected));oracle_draw(&d,alpha_on);
    trace_on=1U;trace_blit_count=trace_fill_count=0U;
    CHECK(garden_render_frame(&d,1U)==GPU_OK,"candidate draw completes");
    trace_on=0U;
    CHECK(!memcmp(expected,fb(sel),sizeof(expected)),"whole RGB565 physical FB equals uncropped independent oracle");
    for(unsigned i=0;i<trace_blit_count;i++) reward_blit_pixels+=(uint64_t)trace_blits[i].w*trace_blits[i].h;
    for(unsigned i=0;i<trace_fill_count;i++) reward_fill_pixels+=(uint64_t)trace_fills[i].w*trace_fills[i].h;
    /* A bounded sparse digest complements the full byte equality above. */
    for(unsigned y=GARDEN_Y0;y<GARDEN_Y0+GARDEN_HEIGHT;y+=13U)
        for(unsigned x=GARDEN_X0;x<GARDEN_X0+GARDEN_WIDTH;x+=11U)
            reward_digest=(reward_digest^expected[y][x])*1099511628211ULL;
    reward_frames++;
}

static void reward_pixel_matrix(void)
{
    static const int positions[3][2]={{320,240},{0,0},{639,479}};
    static const unsigned alphas[4]={0,1,16,32};
    for(unsigned cpu=0;cpu<2U;cpu++) for(unsigned on=0;on<2U;on++) {
        reward_reset(cpu,on);
        for(unsigned id=GARDEN_ASSET_DART_BASE;id<GARDEN_ASSET_FONT_WHITE_BASE;id++)
            for(unsigned p=0;p<3U;p++) for(unsigned a=0;a<4U;a++)
                reward_compare((GardenDraw){(int16_t)positions[p][0],(int16_t)positions[p][1],(uint16_t)id,(uint8_t)alphas[a]},on);
        for(unsigned id=GARDEN_ASSET_EXPLOSION0;id<=GARDEN_ASSET_EXPLOSION3;id++)
            for(unsigned p=0;p<3U;p++) for(unsigned a=0;a<4U;a++)
                reward_compare((GardenDraw){(int16_t)positions[p][0],(int16_t)positions[p][1],(uint16_t)id,(uint8_t)alphas[a]},on);
        /* Every legal fill value, both virtual bar widths, and clipping. */
        for(unsigned boss=0;boss<2U;boss++)
            for(unsigned fill=0;fill<=(boss ? 126U:38U);fill++)
                for(unsigned p=0;p<3U;p++)
                    reward_compare((GardenDraw){(int16_t)positions[p][0],(int16_t)positions[p][1],
                        (uint16_t)(GARDEN_DRAW_BAR_BASE|(boss ? GARDEN_DRAW_BAR_BOSS:0U)|fill),16U},on);
        for(sel=0U;sel<2U;sel++) {
            CHECK(garden_render_frame(NULL,0)==GPU_OK,"retire all cropped composite history");
            CHECK(!memcmp(base,fb(sel),sizeof(base)),"both physical FBs retire without ghost pixels");
        }
    }
}

static void reward_work_matrix(void)
{
    reward_reset(0U,1U);sel=0U;
    unsigned before=calls;
    for(unsigned id=0;id<GARDEN_ASSET_COUNT;id++) {
        GardenDraw d={320,240,(uint16_t)id,0};
        CHECK(garden_render_frame(&d,1U)==GPU_OK,"every valid zero-alpha sprite completes");
    }
    reward_zero_commands=calls-before;
    CHECK(!memcmp(base,fb(0),sizeof(base)),"all zero-alpha sprites preserve the full RGB565 destination");
#ifndef REWARD_RENDER_REFERENCE
    CHECK(reward_zero_commands==0U && garden_render_stats()->sprites==0U,"invisible sprites submit and remember no work");
#endif
    reward_reset(0U,1U);sel=0U;
    GardenDraw full={120,80,GARDEN_DRAW_BAR_BASE|GARDEN_DRAW_BAR_BOSS|126U,32U};
    before=calls;
    CHECK(garden_render_frame(&full,1U)==GPU_OK,"full Boss bar drawn");
    reward_full_bar_commands=calls-before;
#ifndef REWARD_RENDER_REFERENCE
    CHECK(reward_full_bar_commands==2U,"full Boss bar omits the fully covered empty-slot fill");
#endif
    /* Keep invalid input/target failure behavior observable and bounded. */
    reward_reset(0U,1U);sel=0U;
    GardenDraw invalid={100,100,GARDEN_ASSET_COUNT,0};
    CHECK(garden_render_frame(&invalid,1U)==GPU_EHW,"zero alpha does not bypass invalid atlas ID validation");
    invalid.asset=GARDEN_ASSET_DART_BASE;invalid.alpha=33U;
    CHECK(garden_render_frame(&invalid,1U)==GPU_EHW,"zero-work optimization does not bypass Alpha range validation");
    invalid.alpha=0U;reject_target=1U;
    CHECK(garden_render_frame(&invalid,1U)==GPU_EHW,"zero alpha retains the same invalid/front-target guard");
    reject_target=0U;
    GardenDraw crop={100,100,GARDEN_ASSET_DART_BASE,16U};
    memcpy(old_pixels,fb(0),sizeof(old_pixels));fail_glyph=1;
    CHECK(garden_render_frame(&crop,1U)==GPU_EHW,"cropped GPU failure propagates");
    CHECK(garden_render_frame(NULL,0)==GPU_OK && !memcmp(old_pixels,fb(0),sizeof(old_pixels)),"cropped fault history retires completely");
}

int main(int argc,char **argv)
{
    int rc=reward_base_main(argc,argv);if(rc)return rc;
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,(ASSET_BASE_ADDR+ASSET_SIZE_BYTES-FB0_ADDR),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"remap production addresses for focused workload");
    FILE *in=fopen(argv[1],"rb");CHECK(in!=NULL,"open frozen real atlas");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1,GARDEN_ATLAS_BYTES,in)==GARDEN_ATLAS_BYTES && fgetc(in)==EOF,"load exact atlas");fclose(in);
    reward_pixel_matrix();reward_work_matrix();
    printf("REWARD_RENDER frames=%u digest=0x%llx blit_pixels=%llu fill_pixels=%llu zero_commands=%u full_bar_commands=%u checks=%u\n",
        reward_frames,(unsigned long long)reward_digest,(unsigned long long)reward_blit_pixels,
        (unsigned long long)reward_fill_pixels,reward_zero_commands,reward_full_bar_commands,checks);
    VirtualFree(ram,0,MEM_RELEASE);return 0;
}
