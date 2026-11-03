/* Host-only frozen calm-Boss parent; separate symbols and translation unit. */
#define garden_init patrol_ref_init
#define garden_step patrol_ref_step
#define garden_boss_fire_countdown patrol_ref_countdown
#include "../../neon_boss_calm_20261022/src/garden_core.c"
void patrol_ref_enemies(GardenGame *g){run_enemies(g);}
void patrol_ref_fire(GardenGame *g,GardenEnemy *e){enemy_fire(g,e);}
void patrol_ref_boss(GardenGame *g){run_boss(g);}
void patrol_ref_laser(GardenGame *g){run_laser(g);}
void patrol_ref_attack(GardenGame *g,unsigned attack)
{if(attack==1U)boss_attack1(g);else if(attack==2U)boss_attack2(g);else boss_attack3(g);}

