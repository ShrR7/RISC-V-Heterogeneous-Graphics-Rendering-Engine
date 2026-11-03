/* Lossless whole-bullet mixed routing. Reverse conservative occupancy keeps
 * every later legacy layer, including legacy enemy bullets, above this one.
 * No gameplay writes, no asset rescale, no per-bullet opacity approximation. */
#include <stdint.h>
#include <string.h>
#include "garden_assets.h"
#include "garden_render.h"
#include "g9_route.h"
#include "g9_memory.h"
#include "stage9_tiles.h"
#ifndef G9_ROUTE_HOST
#include "vexriscv.h"
#endif
typedef struct {int16_t x,y,vx,vy;uint16_t sprite,flags;uint32_t reserved;} Desc;
typedef struct {int x,y,w,h;} Bounds;
typedef struct {uint8_t skip[GARDEN_DRAW_MAX];uint32_t occupied[75];uint16_t ids[32];} Work;
_Static_assert(sizeof(Desc)==16U,"descriptor ABI");
_Static_assert(sizeof(Work)<G9_ROUTE_BYTES,"route DDR fit");
#define WORK ((Work *)(uintptr_t)G9_ROUTE_ADDR)
static G9RouteStats stats;
static unsigned bank,valid_count;
static void visible(Bounds *b)
{
    int r=b->x+b->w,d=b->y+b->h;
    if(b->x<GARDEN_X0)b->x=GARDEN_X0;
    if(b->y<GARDEN_Y0)b->y=GARDEN_Y0;
    if(r>GARDEN_X0+GARDEN_WIDTH)r=GARDEN_X0+GARDEN_WIDTH;
    if(d>GARDEN_Y0+GARDEN_HEIGHT)d=GARDEN_Y0+GARDEN_HEIGHT;
    b->w=r>b->x?r-b->x:0;b->h=d>b->y?d-b->y:0;
}
static Bounds bounds(const GardenDraw *d)
{
    unsigned id=d->asset;Bounds b={GARDEN_X0+d->x,GARDEN_Y0+d->y,0,0};
    if(id&GARDEN_DRAW_SCORE_BASE){b.w=(int)garden_render_score_width(id&GARDEN_DRAW_SCORE_MAX);b.h=16;}
    else if(id&GARDEN_DRAW_BAR_BASE){b.w=(id&GARDEN_DRAW_BAR_BOSS)?128:40;b.h=8;}
    else if(id&GARDEN_DRAW_LASER_BASE){unsigned a=id&(GARDEN_DRAW_LASER_ACTIVE|GARDEN_DRAW_LASER_FADE);b.x-=a?22:0;b.w=a?45:1;b.h=GARDEN_Y0+GARDEN_HEIGHT-b.y;}
    else {
        unsigned banked=(id&GARDEN_DRAW_BANK_BASE)!=0;
        if(banked){unsigned f=id&3U;id=f?GARDEN_ASSET_PLAYER_FRAME1+f-1U:GARDEN_ASSET_PLAYER;}
        else id=garden_draw_form_source(id);
        const garden_sprite_t *s=garden_asset(id);
        if(s){b.x-=s->anchor_x;b.y-=s->anchor_y;b.w=s->width;b.h=s->height;if(banked){b.x-=12;b.w+=24;}}
    }
    visible(&b);return b;
}
static unsigned occupy(Bounds b,unsigned set)
{
    if(b.y>=480 || !b.w || !b.h)return 0;
    if(b.y+b.h>480)b.h=480-b.y;
    unsigned x0=(unsigned)b.x/16U,x1=(unsigned)(b.x+b.w-1)/16U;
    unsigned y0=(unsigned)b.y/16U,y1=(unsigned)(b.y+b.h-1)/16U,hit=0;
    for(unsigned y=y0;y<=y1;y++)for(unsigned x=x0;x<=x1;x++){
        unsigned bit=y*80U+x;uint32_t mask=1U<<(bit&31U);
        if(set)WORK->occupied[bit>>5]|=mask;else hit|=WORK->occupied[bit>>5]&mask;
    }
    return hit!=0;
}
static uint32_t crc_bytes(uint32_t address,unsigned bytes)
{
    const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)address;uint32_t c=UINT32_MAX;
    for(unsigned i=0;i<bytes;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return c^UINT32_MAX;
}
int g9_route_init(void)
{
    memset(&stats,0,sizeof(stats));valid_count=bank=0;
    return crc_bytes(ST9_TILES_BASE,ST9_TILES_BLOB_BYTES)==ST9_TILES_CRC32?0:-1;
}
void g9_route_clear(void){valid_count=0;}
unsigned g9_route_skip(unsigned i){return i<valid_count && WORK->skip[i];}
const G9RouteStats *g9_route_stats(void){return &stats;}
int g9_route_prepare(const GardenDraw *draws,unsigned count,unsigned enabled)
{
    unsigned frames=stats.frames+1U;memset(&stats,0,sizeof(stats));stats.frames=frames;
    valid_count=0;if(count>GARDEN_DRAW_MAX || (!draws&&count)){stats.errors++;return -1;}
    memset(WORK->occupied,0,sizeof(WORK->occupied));memset(WORK->skip,0,count);
    unsigned used=0,n=0;bank^=1U;
    stats.atlas_addr=bank?G9_ATLAS1_ADDR:G9_ATLAS0_ADDR;
    stats.desc_addr=bank?G9_DESC1_ADDR:G9_DESC0_ADDR;
    if(!enabled){
        for(unsigned i=0;i<count;i++)
            if(garden_asset_is_enemy_bullet(garden_draw_form_source(draws[i].asset)))
                stats.enemies++;
        stats.legacy=stats.enemies;valid_count=count;return 0;
    }
    Desc *desc=(Desc *)(uintptr_t)stats.desc_addr;
    uint32_t *atlas=(uint32_t *)(uintptr_t)stats.atlas_addr;
    if(garden_render_alpha_enabled() && garden_render_panel_is_visible())
        occupy((Bounds){GARDEN_X0+272,GARDEN_Y0+96,96,32},1);
    for(unsigned rev=count;rev>0;rev--){
        unsigned i=rev-1U;const GardenDraw *d=draws+i;unsigned source=garden_draw_form_source(d->asset);
        Bounds b=bounds(d);unsigned enemy=garden_asset_is_enemy_bullet(source);
        if(!enemy){if(d->alpha || !garden_render_alpha_enabled())occupy(b,1);continue;}
        stats.enemies++;unsigned reason=0;
        const volatile Stage9TileAsset *s=stage9_tile_asset(source);
        int left=GARDEN_X0+d->x-s->anchor_x,top=GARDEN_Y0+d->y-s->anchor_y;
        if(!enabled)reason=6;
        else if(d->asset!=source || d->alpha!=32U)reason=1;
        else if(left<GARDEN_X0 || left+s->width>GARDEN_X0+GARDEN_WIDTH || top<GARDEN_Y0 || top+s->height>480)reason=2;
        else if(occupy(b,0))reason=3;
        else if(!s->count || s->count>4U || source>=ST9_TILES_ASSET_COUNT || s->first_entry+s->count>ST9_TILES_ENTRY_COUNT){stats.errors++;return -1;}
        else if(n+s->count>G9_DESC_CAPACITY)reason=5;
        unsigned local[4],add[4],adds=0;
        if(!reason)for(unsigned t=0;t<s->count;t++){
            unsigned id=stage9_tile_entry(s->first_entry+t)->unique_tile_id,k=0;
            if(id>=ST9_TILES_UNIQUE_COUNT){stats.errors++;return -1;}
            while(k<used && WORK->ids[k]!=id)k++;
            if(k==used){unsigned j=0;while(j<adds && add[j]!=id)j++;local[t]=used+j;if(j==adds)add[adds++]=id;}
            else local[t]=k;
        }
        if(!reason && used+adds>32U)reason=4;
        if(reason){
            stats.legacy++;
            if(reason==1)stats.fallback_alpha++;
            if(reason==2)stats.fallback_region++;
            if(reason==3)stats.fallback_occlusion++;
            if(reason==4)stats.fallback_tiles++;
            if(reason==5)stats.fallback_desc++;
            occupy(b,1);continue;
        }
        for(unsigned j=0;j<adds;j++){
            WORK->ids[used+j]=(uint16_t)add[j];
            const volatile uint32_t *src=(const volatile uint32_t *)(uintptr_t)(ST9_TILES_BASE+ST9_TILES_PIXEL_OFFSET+add[j]*512U);
            for(unsigned word=0;word<128U;word++)atlas[(used+j)*128U+word]=src[word];
        }
        used+=adds;WORK->skip[i]=1U;stats.migrated++;
        for(unsigned t=0;t<s->count;t++){
            const volatile Stage9TileEntry *e=stage9_tile_entry(s->first_entry+t);
            desc[n++]=(Desc){(int16_t)(left+e->source_x),(int16_t)(top+e->source_y),0,0,(uint16_t)local[t],1,0};
        }
    }
    /* Restore exact draw order. Tiles within one bullet are disjoint. */
    for(unsigned i=0;i<n/2U;i++){Desc d=desc[i];desc[i]=desc[n-1U-i];desc[n-1U-i]=d;}
    stats.tiles=used;stats.descriptors=n;valid_count=count;
#ifndef G9_ROUTE_HOST
    asm volatile("fence iorw,iorw" ::: "memory");data_cache_invalidate_all();asm volatile("fence iorw,iorw" ::: "memory");
#endif
    return 0;
}
