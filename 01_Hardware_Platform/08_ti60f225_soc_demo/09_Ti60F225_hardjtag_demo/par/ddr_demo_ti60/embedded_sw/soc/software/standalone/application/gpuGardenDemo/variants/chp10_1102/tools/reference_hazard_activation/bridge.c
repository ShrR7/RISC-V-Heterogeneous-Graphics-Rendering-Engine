/* Historical V baseline compiles with its own state in an independent TU. */
#define garden_init legacy_v_init
#define garden_step legacy_v_step
#define garden_boss_fire_countdown legacy_v_countdown
#include "garden_core.c"
unsigned legacy_activation_report(unsigned *max_age)
{
    GardenGame g;legacy_v_init(&g,12345U);unsigned contacts=0U;*max_age=0U;
    for(unsigned step=0U;step<5000U;step++){
        unsigned prior=g.tick;legacy_v_step(&g,0U);if(g.tick<prior)break;
        for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++){
            GardenEnemyBullet *b=&g.enemy_bullets[i];if(!b->active || b->cord_wait<0)continue;
            if(b->age>*max_age)*max_age=b->age;
            if(g.mode<=GARDEN_MODE_BOSS && g.player.active && !g.player.grace && !g.player.respawning &&
               overlaps(b->x,b->y,g.player.x,g.player.y,9000,9000))contacts++;
        }
    }
    return contacts;
}
