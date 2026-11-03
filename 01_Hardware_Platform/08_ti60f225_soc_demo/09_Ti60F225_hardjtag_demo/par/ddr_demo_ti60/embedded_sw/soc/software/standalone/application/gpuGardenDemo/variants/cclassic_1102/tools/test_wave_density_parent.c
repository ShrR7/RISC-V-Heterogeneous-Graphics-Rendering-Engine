/* Host-only reference: frozen parent production core in its own TU. */
#define garden_init wave_ref_init
#define garden_step wave_ref_step
#define garden_boss_fire_countdown wave_ref_countdown
#include "../../neon_batch_lazy_20261021/src/garden_core.c"
void wave_ref_enemy_fire(GardenGame *g,GardenEnemy *e){enemy_fire(g,e);}
void wave_ref_run_enemies(GardenGame *g){run_enemies(g);}
void wave_ref_boss_attack(GardenGame *g,unsigned attack)
{if(attack==1U)boss_attack1(g);else if(attack==2U)boss_attack2(g);else boss_attack3(g);}
