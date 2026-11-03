/* Host-only frozen fixed-wave parent in separate TU. */
#define garden_init calm_ref_init
#define garden_step calm_ref_step
#define garden_boss_fire_countdown calm_ref_countdown
#include "../../neon_wave_fixed_20261022/src/garden_core.c"
void calm_ref_boss(GardenGame *g){run_boss(g);}
void calm_ref_laser(GardenGame *g){run_laser(g);}
void calm_ref_start(GardenGame *g){start_boss(g);}
void calm_ref_enemies(GardenGame *g){run_enemies(g);}
void calm_ref_attack(GardenGame *g,unsigned attack){if(attack==1U)boss_attack1(g);else if(attack==2U)boss_attack2(g);else boss_attack3(g);}
