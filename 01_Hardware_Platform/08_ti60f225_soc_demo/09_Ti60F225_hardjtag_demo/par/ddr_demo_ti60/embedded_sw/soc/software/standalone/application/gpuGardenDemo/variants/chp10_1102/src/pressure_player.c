/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pressure_player.h"
#include "garden_assets.h"
#include "garden_core.h"
#include "gpu_driver.h"
#include "gpu_display.h"
#include "pad_hw.h"
#pragma GCC optimize("Os")

static const garden_sprite_t *sprite;
static uint32_t old[2],previous;
static int32_t px,py; /* physical top-left, Q8 pixels */
#define CACHE(sel) (0x009c0000U+((sel)<<13))

int pressure_player_init(void)
{
    old[0]=old[1]=previous=0U;
    sprite=garden_asset(GARDEN_ASSET_PLAYER);
    if(!sprite || !sprite->width || !sprite->height ||
       sprite->width>64U || sprite->height>64U){sprite=0;return GPU_EINVAL;}
    px=(640-(int)sprite->width/2)*256;
    py=(530-(int)sprite->height/2)*256;
    return GPU_OK;
}
static int target(uint32_t fb,unsigned sel)
{
    return sprite && sel<2U && fb==display_back_addr() && sel==display_back_sel();
}
int pressure_player_restore(uint32_t fb,unsigned sel)
{
    if(!target(fb,sel))return GPU_ESTATE;
    if(!old[sel])return GPU_OK;
    int rc=gpu_copy(CACHE(sel),old[sel],sprite->width,sprite->height,128U,2560U);
    if(!rc)old[sel]=0U;
    return rc;
}
int pressure_player_draw(uint32_t fb,unsigned sel,uint32_t now_ms)
{
    if(!target(fb,sel) || old[sel])return GPU_ESTATE;
    uint32_t elapsed=previous ? now_ms-previous:0U;previous=now_ms;
    if(elapsed>50U)elapsed=50U;
    unsigned held=pad_hw_held(now_ms);
    int dx=!!(held&GARDEN_INPUT_RIGHT)-!!(held&GARDEN_INPUT_LEFT);
    int dy=!!(held&GARDEN_INPUT_DOWN)-!!(held&GARDEN_INPUT_UP);
    int speed=(held&GARDEN_INPUT_SLOW) ? 31:61;
    if(dx && dy)speed=(speed*181)>>8;
    px+=dx*(int)elapsed*speed;py+=dy*(int)elapsed*speed;
    int xmax=(960-(int)sprite->width)*256,ymax=(600-(int)sprite->height)*256;
    if(px<320*256)px=320*256;
    if(px>xmax)px=xmax;
    if(py<120*256)py=120*256;
    if(py>ymax)py=ymax;
    uint32_t dst=fb+(uint32_t)(py>>8)*2560U+(uint32_t)(px>>8)*2U;
    int rc=gpu_copy(dst,CACHE(sel),sprite->width,sprite->height,2560U,128U);
    if(rc)return rc;
    old[sel]=dst; /* Also recover a partially drawn sprite after a GPU error. */
    return gpu_bitblt(GARDEN_ATLAS_ADDRESS+sprite->offset,dst,sprite->width,
                      sprite->height,sprite->width*2U,2560U,GARDEN_KEY_COLOR);
}
