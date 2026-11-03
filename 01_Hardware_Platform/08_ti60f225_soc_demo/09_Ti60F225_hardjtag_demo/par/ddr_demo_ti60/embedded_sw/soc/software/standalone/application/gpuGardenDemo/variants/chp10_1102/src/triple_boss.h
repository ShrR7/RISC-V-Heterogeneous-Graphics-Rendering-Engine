/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "pure_bullet_test.h"
#define TRIPLE_PER_RAY_MAX 33U
#define TRIPLE_BULLET_MAX (TRIPLE_PER_RAY_MAX*PURE_RAY_COUNT)
void triple_boss_init(PureBulletParticle *p,unsigned count,const PureBulletRayMeta *rays,unsigned speed);
int triple_boss_build(PureBulletParticle *p,PureBulletDesc *desc,uint32_t *legacy,unsigned count,
    uint32_t fb,const PureBulletRayMeta *rays,unsigned speed,PureBulletBuildResult *out);
int triple_boss_backdrop(void);
