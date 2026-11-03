/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "g9_memory.h"
#include "gpu_regs.h"
#include <string.h>
#ifndef G9_MEMORY_HOST_TEST
#include "vexriscv.h"
#endif

_Static_assert(sizeof(GardenEnemyBullet)==36U,"enemy bullet ABI");
_Static_assert(sizeof(GardenGame)==74616U,"2000-slot game ABI");
_Static_assert(sizeof(GardenMotionSlot)==12U,"motion slot ABI");
_Static_assert(sizeof(GardenMotion)==24356U,"2000-slot motion ABI");
_Static_assert(sizeof(GardenDraw)==8U,"draw DDR ABI");
_Static_assert(sizeof(G9MemoryRect)==8U,"history DDR ABI");
_Static_assert(sizeof(G9MemoryDescWords)==16U,"descriptor DDR ABI");
_Static_assert(GARDEN_MAX_ENEMY_BULLETS<=G9_DESC_CAPACITY,"descriptor capacity");
_Static_assert(GARDEN_BOSS_BULLET_BUDGET==80U,"accepted Boss admission budget");
_Static_assert(sizeof(GardenGame)+G9_GUARD_BYTES<=G9_GAME_RESERVE_BYTES,"game reservation");
_Static_assert(sizeof(GardenMotion)+G9_GUARD_BYTES<=G9_MOTION_RESERVE_BYTES,"motion reservation");
_Static_assert(sizeof(G9_DRAWS)==G9_DRAWS_BYTES,"draw array bytes");
_Static_assert(sizeof(G9_HISTORY)==G9_HISTORY_BYTES,"history array bytes");
_Static_assert(ASSET_BASE_ADDR+ASSET_SIZE_BYTES+G9_GUARD_BYTES<=G9_DESC0_ADDR-G9_GUARD_BYTES,"legacy assets and scratch protected");
_Static_assert(FB0_ADDR+FB_SIZE_BYTES+G9_GUARD_BYTES<=G9_DESC0_ADDR-G9_GUARD_BYTES,"FB0 protected");
_Static_assert(FB1_ADDR+FB_SIZE_BYTES+G9_GUARD_BYTES<=G9_DESC0_ADDR-G9_GUARD_BYTES,"FB1 protected");
#define G9_GAP(a,n,b) ((a)+(n)+G9_GUARD_BYTES<=(b)-G9_GUARD_BYTES)
_Static_assert(G9_GAP(G9_DESC0_ADDR,G9_DESC_BYTES,G9_DESC1_ADDR),"descriptor banks overlap");
_Static_assert(G9_GAP(G9_DESC1_ADDR,G9_DESC_BYTES,G9_ATLAS0_ADDR),"descriptor and atlas overlap");
_Static_assert(G9_GAP(G9_ATLAS0_ADDR,G9_ATLAS_BYTES,G9_ATLAS1_ADDR),"atlas banks overlap");
_Static_assert(G9_GAP(G9_ATLAS1_ADDR,G9_ATLAS_BYTES,G9_OLD_BG_ADDR),"old background overlap");
_Static_assert(G9_GAP(G9_OLD_BG_ADDR,G9_OLD_BG_BYTES,G9_GAME_ADDR),"old background and game overlap");
_Static_assert(G9_GAP(G9_GAME_ADDR,G9_GAME_RESERVE_BYTES,G9_MOTION_ADDR),"game and motion overlap");
_Static_assert(G9_GAP(G9_MOTION_ADDR,G9_MOTION_RESERVE_BYTES,G9_DRAWS_ADDR),"motion and draws overlap");
_Static_assert(G9_GAP(G9_DRAWS_ADDR,G9_DRAWS_BYTES,G9_HISTORY_ADDR),"draws and history overlap");
_Static_assert(G9_GAP(G9_HISTORY_ADDR,G9_HISTORY_BYTES,G9_ROUTE_ADDR),"history and route overlap");
_Static_assert(G9_GAP(G9_ROUTE_ADDR,G9_ROUTE_BYTES,G9_LOG_ADDR),"route and log overlap");
_Static_assert(G9_LOG_ADDR+G9_LOG_BYTES+G9_GUARD_BYTES<=G9_DDR_END,"DDR envelope");
_Static_assert(((G9_DESC0_ADDR|G9_DESC1_ADDR|G9_ATLAS0_ADDR|G9_ATLAS1_ADDR)&15U)==0U,"engine input16-byte alignment");

typedef struct { uint32_t addr,bytes; } G9MemoryBlock;
static const G9MemoryBlock blocks[]={
    {G9_DESC0_ADDR,G9_DESC_BYTES},{G9_DESC1_ADDR,G9_DESC_BYTES},
    {G9_ATLAS0_ADDR,G9_ATLAS_BYTES},{G9_ATLAS1_ADDR,G9_ATLAS_BYTES},
    {G9_GAME_ADDR,sizeof(GardenGame)},{G9_MOTION_ADDR,sizeof(GardenMotion)},
    {G9_DRAWS_ADDR,G9_DRAWS_BYTES},{G9_HISTORY_ADDR,G9_HISTORY_BYTES},
    {G9_ROUTE_ADDR,G9_ROUTE_BYTES},{G9_LOG_ADDR,G9_LOG_BYTES}
};
static void memory_fence(void)
{
#ifdef G9_MEMORY_HOST_TEST
    extern void g9_memory_host_fence(void);
    g9_memory_host_fence();
#else
    asm volatile("fence iorw,iorw" ::: "memory");
    data_cache_invalidate_all();
    asm volatile("fence iorw,iorw" ::: "memory");
#endif
}
static uint32_t guard_word(unsigned block,unsigned side,unsigned word)
{return UINT32_C(0xC9201000)^((block+1U)*UINT32_C(0x01010701))^(side*UINT32_C(0x10204080))^(word*UINT32_C(0x01020304));}

void g9_memory_guard_init(void)
{
    for(unsigned i=0U;i<sizeof(blocks)/sizeof(blocks[0]);i++) {
        volatile uint32_t *before=(volatile uint32_t *)(uintptr_t)(blocks[i].addr-G9_GUARD_BYTES);
        volatile uint32_t *after=(volatile uint32_t *)(uintptr_t)(blocks[i].addr+blocks[i].bytes);
        for(unsigned w=0U;w<4U;w++) {
            before[w]=guard_word(i,0U,w);after[w]=guard_word(i,1U,w);
        }
    }
    memory_fence();
}
unsigned g9_memory_guard_check(void)
{
    unsigned bad=0U;memory_fence();
    for(unsigned i=0U;i<sizeof(blocks)/sizeof(blocks[0]);i++) {
        const volatile uint32_t *before=(const volatile uint32_t *)(uintptr_t)(blocks[i].addr-G9_GUARD_BYTES);
        const volatile uint32_t *after=(const volatile uint32_t *)(uintptr_t)(blocks[i].addr+blocks[i].bytes);
        for(unsigned w=0U;w<4U;w++) {
            bad+=before[w]!=guard_word(i,0U,w);bad+=after[w]!=guard_word(i,1U,w);
        }
    }
    return bad;
}
void g9_memory_reset(void)
{
    memset(G9_GAME_PTR,0,sizeof(GardenGame));
    memset(G9_MOTION_PTR,0,sizeof(GardenMotion));
    memset(G9_DRAWS,0,sizeof(G9_DRAWS));
    memset(G9_HISTORY,0,sizeof(G9_HISTORY));
    memset(G9_ROUTE,0,sizeof(G9_ROUTE));
    memory_fence();
}
