/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production renderer, stage and ordnance code, host RAM/GPU mocks only.
 * Reuse established clipped-command mocks, not their old baseline tests. */
#define main inherited_batch_test_main
#include "test_batch_render.c"
#undef main
#include "garden_stage.h"
#include "garden_ordnance_view.h"
#include "g9_memory.h"

unsigned g9_route_skip(unsigned index) { (void)index;return 0U; }
int gpu_wait_idle(uint32_t timeout) { (void)timeout;return fake_busy ? GPU_ETIMEOUT:GPU_OK; }

static uint8_t *baseline;
static unsigned compared_frames;
static void oracle_sprite(const GardenDraw *d)
{
    const garden_sprite_t *s=garden_asset(d->asset);
    CHECK(s!=NULL,"real visual asset exists");
    int left=GARDEN_X0+d->x-s->anchor_x,top=GARDEN_Y0+d->y-s->anchor_y;
    for(unsigned row=0U;row<s->height;row++) for(unsigned col=0U;col<s->width;col++) {
        int x=left+(int)col,y=top+(int)row;
        if(x<GARDEN_X0 || x>=GARDEN_X0+GARDEN_WIDTH || y<GARDEN_Y0 || y>=GARDEN_Y0+GARDEN_HEIGHT) continue;
        uint16_t p=*(uint16_t *)(uintptr_t)(ASSET_BASE_ADDR+s->offset+row*garden_sprite_stride(s)+col*2U);
        if(p==GARDEN_KEY_COLOR) continue;
        uint16_t *out=(uint16_t *)(reference+(unsigned)y*DISPLAY_STRIDE_BYTES+(unsigned)x*2U);
        *out=d->alpha==32U ? p:gpu_alpha_rgb565(p,*out,d->alpha);
    }
}
static void warning_pixels(void)
{
    static const unsigned ages[]={0U,1U,7U,8U,9U,16U,99U,100U,104U};
    for(unsigned mode=0U;mode<3U;mode++) {
        GardenGame game;memset(&game,0,sizeof(game));GardenStage stage;
        garden_stage_reset(&stage);game.tick=1;game.mode=GARDEN_MODE_WAVES;garden_stage_observe(&stage,&game);
        game.tick=2;game.mode=GARDEN_MODE_BOSS;game.boss.active=1;garden_stage_observe(&stage,&game);
        reset(mode);
        for(sel=0U;sel<2U;sel++) CHECK(garden_render_frame(NULL,0U)==GPU_OK,"initialize fixed panels and both histories");
        memcpy(baseline,fb(0U),FB_SIZE_BYTES);
        for(unsigned t=0U;t<sizeof(ages)/sizeof(ages[0]);t++) {
            unsigned age=ages[t];game.tick=age+2U;garden_stage_observe(&stage,&game);
            uint16_t color=age<100U ? ((age/8U)&1U ? 0x9800U:0x3000U):0U;
            for(sel=0U;sel<2U;sel++) {
                GardenDraw d[2]={{(int16_t)(60U+t*63U),(int16_t)(60U+t*41U),GARDEN_ASSET_PLAYER,32U},
                                  {(int16_t)(610U-t*45U),(int16_t)(470U-t*30U),GARDEN_ASSET_LARGE_BLAST_BASE+(uint16_t)(t%8U),20U}};
                memcpy(other,fb(sel^1U),FB_SIZE_BYTES);memcpy(reference,baseline,FB_SIZE_BYTES);
                if(color) for(int y=GARDEN_Y0;y<GARDEN_Y0+GARDEN_HEIGHT;y++)
                    for(int x=GARDEN_X0;x<GARDEN_X0+GARDEN_WIDTH;x++)
                        *(uint16_t *)(reference+(unsigned)y*DISPLAY_STRIDE_BYTES+(unsigned)x*2U)=color;
                for(unsigned i=0U;i<2U;i++) oracle_sprite(d+i);
                CHECK(garden_stage_prepare_frame(&stage,&game,sel)>=0,"warning selects matching full-field tint");
                CHECK(garden_render_frame(d,2U)==GPU_OK,"production warning field frame completes");
                CHECK(!memcmp(reference,fb(sel),FB_SIZE_BYTES),"all RGB565 pixels: tint, moving sprite erases, warning exit, both buffers, CPU/single/list");
                CHECK(!memcmp(other,fb(sel^1U),FB_SIZE_BYTES),"front buffer remains unchanged");
                compared_frames++;
            }
        }
        /* The banner is drawn above moving actors and must repaint even while
         * a pulse's value has not changed. It stays in the game field/footer. */
        game.tick=3U;stage.last_tick=3U;stage.boss_entry_tick=2U;stage.boss_entry_valid=1U;
        garden_stage_ui_reset(&stage);sel=0U;
        memcpy(other,fb(sel),FB_SIZE_BYTES);
        CHECK(garden_stage_draw(&stage,&game,sel)==1,"central warning is drawn");
        CHECK(garden_stage_draw(&stage,&game,sel)==1,"warning never falsely caches over moving actors");
        for(int y=0;y<(int)DISPLAY_HEIGHT;y++) for(int x=0;x<(int)DISPLAY_WIDTH;x++) {
            unsigned in_field=x>=GARDEN_X0 && x<GARDEN_X0+GARDEN_WIDTH && y>=GARDEN_Y0 && y<GARDEN_Y0+GARDEN_HEIGHT;
            unsigned in_footer=x>=504 && x<948 && y>=634 && y<678;
            if(!in_field && !in_footer) CHECK(!memcmp(other+(unsigned)y*DISPLAY_STRIDE_BYTES+(unsigned)x*2U,fb(sel)+(unsigned)y*DISPLAY_STRIDE_BYTES+(unsigned)x*2U,2U),"warning leaves engineering panels untouched");
        }
    }
}

static void bomb_contract(void)
{
    GardenGame game;memset(&game,0,sizeof(game));
    game.ordnance=(GardenBomb){.x=320000,.y=210000,.age=5U,.active=1U};
    game.pickups[0]=(GardenPickup){.x=50000,.y=70000,.active=1U,.kind=GARDEN_PICKUP_CORE};
    game.pickups[1]=(GardenPickup){.x=150000,.y=170000,.active=1U,.kind=GARDEN_PICKUP_HEAL};
    game.reward.pickup_flash_ticks=16U;game.reward.pickup_flash_kind=GARDEN_PICKUP_HEAL;
    GardenDraw original[4]={{320,100,GARDEN_ASSET_BOSS1,32},{320,300,GARDEN_ASSET_SHOT,32},
                          {50,350,GARDEN_ASSET_DART_BASE,32},{300,50,GARDEN_DRAW_SCORE_BASE+95,32}};
    CHECK(garden_asset(GARDEN_ASSET_LARGE_BOMB_BASE)->width>garden_asset(GARDEN_ASSET_ORDNANCE_BOMB_BASE)->width,"thrown bomb width is larger");
    CHECK(garden_asset(GARDEN_ASSET_LARGE_BLAST_BASE)->width>garden_asset(GARDEN_ASSET_ORDNANCE_BLAST_BASE)->width,"main impact blast is larger");
    for(unsigned blast=0U;blast<=GARDEN_BOMB_BLAST_TICKS;blast++) for(unsigned alpha=0U;alpha<2U;alpha++) {
        game.ordnance.blast_ticks=(uint8_t)blast;GardenGame unchanged=game;
        unsigned reserve=garden_ordnance_view_reserve(&game);
        CHECK(reserve==(blast ? 14U:4U),"exact bounded admission includes larger multilayer impact");
        for(unsigned capacity=4U;capacity<=20U;capacity++) {
            GardenDraw d[22];memset(d,0,sizeof(d));memcpy(d,original,sizeof(original));
            d[capacity]=(GardenDraw){-123,456,0xBEEFU,31U};
            unsigned n=garden_ordnance_view_build(&game,d,4U,capacity,alpha),added=n-4U;
            CHECK(n<=capacity && d[capacity].asset==0xBEEFU,"effects obey capacity and retain canary");
            CHECK(n==4U+(reserve<capacity-4U ? reserve:capacity-4U),"reserve and actual assembly agree");
            CHECK(!memcmp(d,original,sizeof(GardenDraw)),"body remains before local effects");
            for(unsigned i=1U;i<4U;i++) CHECK(!memcmp(d+i+added,original+i,sizeof(GardenDraw)),"real bullets and damage labels retain exact relative order");
            for(unsigned i=1U;i<=added;i++) {
                CHECK(garden_asset(d[i].asset)!=NULL && d[i].alpha<=32U,"all effect asset and alpha values valid");
                if(!alpha) CHECK(d[i].alpha==32U,"alpha-off fallback stays opaque");
            }
            if(added) CHECK(d[1].asset>=(blast ? GARDEN_ASSET_LARGE_BLAST_BASE:GARDEN_ASSET_LARGE_BOMB_BASE) && d[1].x==320 && d[1].y==210,"priority larger bomb/blast uses exact real centre");
            CHECK(!memcmp(&unchanged,&game,sizeof(game)),"visual effects never change damage, collision or entity state");
        }
    }
    game.pickups[0].active=game.pickups[1].active=0U;game.reward.pickup_flash_ticks=0U;
    for(unsigned age=0U;age<GARDEN_BOMB_BLAST_TICKS;age++) {
        game.ordnance.blast_ticks=(uint8_t)(GARDEN_BOMB_BLAST_TICKS-age);
        GardenDraw d[6];memcpy(d,original,sizeof(original));d[4]=(GardenDraw){32767,-32768,GARDEN_ASSET_PLAYER,32U};
        d[5]=(GardenDraw){12,13,0xBEEFU,32U};GardenGame unchanged=game;
        garden_ordnance_camera_apply(&game,d,5U);
        int dx=d[0].x-original[0].x,dy=d[0].y-original[0].y;
        CHECK(abs(dx)<=6 && abs(dy)<=6,"shake bounded to six pixels");
        for(unsigned i=1U;i<4U;i++) CHECK(d[i].x-original[i].x==dx && d[i].y-original[i].y==dy && d[i].asset==original[i].asset,"all world sprites and damage move uniformly after interpolation");
        if(age>=10U) CHECK(dx==0 && dy==0,"shake decays completely before blast ends");
        CHECK(d[5].asset==0xBEEFU && d[5].x==12 && d[5].y==13,"shake obeys list bound");
        CHECK(d[4].x>32000 && d[4].y<-32000,"shake clamps signed coordinates instead of wrapping");
        CHECK(!memcmp(&game,&unchanged,sizeof(game)),"camera presentation is read-only");
    }
    /* Actual blast bounds and dirty restoration, including all four clipped
     * corners, are compared against an independent atlas sampling oracle. */
    for(unsigned mode=0U;mode<3U;mode++) {
        reset(mode);for(sel=0U;sel<2U;sel++) CHECK(garden_render_frame(NULL,0U)==GPU_OK,"initialize blast pixel buffers");
        memcpy(baseline,fb(0U),FB_SIZE_BYTES);
        for(unsigned f=0U;f<24U;f++) {
            game.ordnance.x=(f%4U<2U ? 4:636)*GARDEN_GRAIN;
            game.ordnance.y=(f%4U==0U || f%4U==3U ? 4:476)*GARDEN_GRAIN;
            game.ordnance.blast_ticks=(uint8_t)(16U-f%16U);
            GardenDraw d[16];unsigned n=garden_ordnance_view_build(&game,d,0U,16U,1U);
            garden_ordnance_camera_apply(&game,d,n);sel=f&1U;
            memcpy(reference,baseline,FB_SIZE_BYTES);memcpy(other,fb(sel^1U),FB_SIZE_BYTES);
            for(unsigned i=0U;i<n;i++) oracle_sprite(d+i);
            CHECK(garden_render_frame(d,n)==GPU_OK,"all enlarged blast pieces render safely");
            CHECK(!memcmp(reference,fb(sel),FB_SIZE_BYTES),"full blast RGB565 equality across clipped bounds, alpha, dirty histories and backends");
            CHECK(!memcmp(other,fb(sel^1U),FB_SIZE_BYTES),"blast cannot write front buffer");compared_frames++;
        }
        for(sel=0U;sel<2U;sel++) {
            CHECK(garden_render_frame(NULL,0U)==GPU_OK,"final blast erase completes");
            CHECK(!memcmp(baseline,fb(sel),FB_SIZE_BYTES),"no blast trails after effect expires");
        }
    }
}

static void pulse_failure(void)
{
    reset(2U);for(sel=0U;sel<2U;sel++) CHECK(garden_render_frame(NULL,0U)==GPU_OK,"initialize fault recovery field");
    sel=0U;memcpy(baseline,fb(sel),FB_SIZE_BYTES);
    garden_render_field_color(0x9800U);fail_fill=1U;
    CHECK(garden_render_frame(NULL,0U)==GPU_EHW && fake_busy,"partial warning full-field list failure is reported");
    CHECK(garden_render_frame(NULL,0U)==GPU_EHW,"warning retry does not bypass in-flight failed GPU list");
    fake_busy=0U;
    /* Let the warning expire before recovery: a partial red fill must not
     * survive merely because the requested colour now equals the old black. */
    garden_render_field_color(0U);
    CHECK(garden_render_frame(NULL,0U)==GPU_OK,"recover only once timed-out engine is idle");
    CHECK(!memcmp(baseline,fb(sel),FB_SIZE_BYTES),"failed pulse also restores fully when warning expires during recovery");
}

int main(int argc,char **argv)
{
    CHECK(argc==2,"atlas path required");
    void *ram=VirtualAlloc((void *)(uintptr_t)FB0_ADDR,0x01000000U-FB0_ADDR,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)(uintptr_t)FB0_ADDR,"map production framebuffer, atlas and DDR history in host RAM");
    FILE *file=fopen(argv[1],"rb");CHECK(file!=NULL,"real generated atlas opens");
    CHECK(fread((void *)(uintptr_t)ASSET_BASE_ADDR,1U,GARDEN_ATLAS_BYTES,file)==GARDEN_ATLAS_BYTES && fgetc(file)==EOF,"real atlas byte length matches metadata");fclose(file);
    reference=malloc(FB_SIZE_BYTES);other=malloc(FB_SIZE_BYTES);baseline=malloc(FB_SIZE_BYTES);
    CHECK(reference && other && baseline,"host pixel oracle buffers allocated");
    warning_pixels();bomb_contract();pulse_failure();
    printf("PASS warning_bomb checks=%u rgb565_frames=%u CPU_GPU_batch=1 physical_buffers=2 core_unchanged=1 board_acceptance=0\n",checks,compared_frames);
    free(baseline);free(reference);free(other);VirtualFree(ram,0U,MEM_RELEASE);return 0;
}
