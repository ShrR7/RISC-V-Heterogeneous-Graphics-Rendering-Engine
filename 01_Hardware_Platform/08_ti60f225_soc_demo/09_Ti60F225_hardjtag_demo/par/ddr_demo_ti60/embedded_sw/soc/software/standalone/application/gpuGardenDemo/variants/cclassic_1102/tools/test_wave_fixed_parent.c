/* Host-only frozen low-density parent, isolated symbols. */
#define garden_init fixed_ref_init
#define garden_step fixed_ref_step
#define garden_boss_fire_countdown fixed_ref_countdown
#include "../../neon_wave_light_20261022/src/garden_core.c"
void fixed_ref_boss_attack(GardenGame *g,unsigned attack)
{if(attack==1U)boss_attack1(g);else if(attack==2U)boss_attack2(g);else boss_attack3(g);}
