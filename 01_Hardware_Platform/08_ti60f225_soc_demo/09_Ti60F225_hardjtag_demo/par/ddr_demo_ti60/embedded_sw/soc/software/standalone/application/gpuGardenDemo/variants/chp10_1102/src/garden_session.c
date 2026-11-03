/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_session.h"
void garden_session_reset(GardenSession *s,const GardenGame *g,uint32_t now_ms,
                          uint32_t serial)
{
    *s=(GardenSession){now_ms,serial,g->mode,0U};
}
uint16_t garden_session_update(GardenSession *s,GardenGame *g,uint32_t now_ms,
                               uint32_t serial,uint32_t activity_ms)
{
    if(s->mode!=g->mode){
        s->mode=g->mode;s->restart_sent=0U;
        /* GAME OVER's idle interval begins when the result is entered, not
         * at an old activity that may precede the death by many seconds. */
        if(g->mode==GARDEN_MODE_GAMEOVER || g->mode==GARDEN_MODE_WAVES)
            s->last_activity_ms=now_ms;
    }
    if(serial!=s->activity_serial){
        s->activity_serial=serial;s->last_activity_ms=activity_ms;
        s->restart_sent=0U;
        if(g->mode==GARDEN_MODE_WAVES || g->mode==GARDEN_MODE_BOSS ||
           g->mode==GARDEN_MODE_GAMEOVER)g->demo_mode=0U;
    }
    uint32_t idle=now_ms-s->last_activity_ms;
    if(g->mode==GARDEN_MODE_GAMEOVER && idle>=15000U && !s->restart_sent){
        s->restart_sent=1U;g->demo_mode=1U;return GARDEN_INPUT_RESTART;
    }
    if((g->mode==GARDEN_MODE_WAVES || g->mode==GARDEN_MODE_BOSS) && idle>=10000U)
        g->demo_mode=1U;
    return 0U;
}
