/* SPDX-License-Identifier: GPL-3.0-or-later
 * The pinned U source and its own U header compile in a separate TU. No old
 * GardenGame pointer is ever passed across the new, larger V state boundary.
 */
#define garden_init legacy_init
#define garden_step legacy_step
#define garden_boss_fire_countdown legacy_countdown
#include "garden_core.c"
unsigned legacy_crossing(int dx,int dy,int vx,int vy,int wait,unsigned grace,
                         unsigned buttons,unsigned dart)
{
    GardenGame g;legacy_init(&g,12345U);g.tick=10U;g.demo_mode=0U;
    g.player.grace=(uint16_t)grace;g.player.recycle=200U;
    g.enemy_bullets[0]=(GardenEnemyBullet){.active=1U,
        .type=dart ? GARDEN_BULLET_DART:GARDEN_BULLET_DIAMOND,
        .x=g.player.x+dx,.y=g.player.y+dy,.vx=vx,.vy=vy,.ttl=500U,
        .cord_wait=(int16_t)wait,.age=59U};
    legacy_step(&g,(uint16_t)buttons);return g.reward.hits_taken;
}
