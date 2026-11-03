/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PURE_BULLET_TEST_H
#define PURE_BULLET_TEST_H
#include <stdint.h>
#include "pure_bullet_scatter16.h"

#define PURE_BULLET_MAX 2730U
#define PURE_BULLET_STORAGE_CAPACITY 3003U
#define PURE_PER_RAY_MIN 8U
#define PURE_PER_RAY_MAX 30U
#define PURE_SPEED_MIN_Q8 512U
#define PURE_SPEED_MAX_Q8 1024U
#define PURE_SPEED_DEFAULT_Q8 612U
#define PURE_RAY_COUNT 91U
#define PURE_BULLET_Q_SHIFT 14U
#define PURE_BULLET_Q_ONE (1U<<PURE_BULLET_Q_SHIFT)
#define PURE_BULLET_WIDTH 640U
#define PURE_BULLET_HEIGHT 480U
#define PURE_BULLET_TILE 16U
#define PURE_BULLET_ORIGIN_X 320U
#define PURE_BULLET_ORIGIN_Y 120U
#define PURE_BULLET_SPRITE_WIDTH 16U
#define PURE_BULLET_SPRITE_HEIGHT 16U
#define PURE_BULLET_FB_STRIDE 2560U
#define PURE_BULLET_SOURCE_X 320U
#define PURE_BULLET_SOURCE_Y 16U

typedef struct {
    uint16_t age,ttl;          /* First frame whose full tile crosses the playfield edge. */
} PureBulletParticle;
typedef struct {
    int16_t x,y,vx,vy;
    uint16_t sprite,flags;
    uint32_t reserved;
} PureBulletDesc;

/* Place count/91 slots evenly along each ray's own in-bounds path. No fixed TTL
 * expires a visible bullet; each slot returns to the emitter at the edge. */
void pure_bullet_test_model_init(PureBulletParticle *state,unsigned count,
    const PureBulletPlan *plans,const PureBulletRayMeta *rays,unsigned speed_q8);

typedef struct {
    uint32_t changed,visible,checksum,s9_count,respawn;
    uint16_t legacy_count[27],family_count[7];
} PureBulletBuildResult;
/* Advance common radial speed; reset only when next full tile would cross the
 * playfield edge. All directions have equal per-ray population and speed. A plan identifies a ray,
 * game family, adapted tile and physical anchor. Legacy uses27 stable
 * buckets of256 destinations. Count0 is allowed during real warmup.
 * NULL pointers or count>2730 return-1 after zeroing a non-NULL result.
 * Invalid plan/ray records return-1; a full bucket returns-2 before an
 * out-of-bounds write. S9 capacity2048 returns-3 before its next write. Records are CRC-verified before firmware calls this.
 * Input/output buffers and result must not overlap. */
int pure_bullet_test_model_build(PureBulletParticle *state,
    PureBulletDesc *s9,uint32_t *legacy_destinations,unsigned count,
    uint32_t framebuffer,const PureBulletPlan *plans,const PureBulletRayMeta *rays,
    unsigned speed_q8,
    PureBulletBuildResult *result);

/* Owns the frame pump and UART until T. Does not step or write GardenGame.
 * 0: user returned, Stage9 stopped, renderer mode/batch flags restored.
 * <0: protection failed; preserve hardware evidence and let main fail().
 * Caller must rebuild both normal-game FB surfaces/history on normal return. */
int pure_bullet_test_run(void);
/* B enters independent three-boss stress; return2 requests restart. */
int pure_bullet_test_boss_run(void);
#endif
