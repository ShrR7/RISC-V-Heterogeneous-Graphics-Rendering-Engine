/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual extracted production union/rebuild function, not a mirrored layout.
 * B capacity failures are recorded as bounded limits, never board passes. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "pressure_storage_actual.h"

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static PureStorage storage;
static PureBulletDesc desc[2048],reference_desc[2048];
static PureBulletRayMeta rays[91];
static PureBulletParticle reference_particles[PURE_BULLET_MAX];
static PureBulletPlan reference_plans[PURE_BULLET_MAX];
static uint32_t reference_legacy[27*256];

static void sentinels(void)
{
    for(unsigned i=0;i<4;i++){
        CHECK(storage.before[i]==0xC200B000U+i);
        CHECK(storage.after[i]==0xC200A000U+i);
    }
}

int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"usage: %s scatter.bin\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");CHECK(file);
    CHECK(!fseek(file,PURE_SCATTER_RAY_OFFSET,SEEK_SET));
    CHECK(fread(rays,sizeof rays,1,file)==1U);fclose(file);
    CHECK(offsetof(PureStorage,legacy)==22000U);
    CHECK(sizeof(storage.triple_particles)==21840U);
    CHECK(TRIPLE_BULLET_MAX==3003U && PURE_BULLET_MAX==2730U);
    printf("STORAGE bytes=%u legacy_offset=%u union_bytes=%u triple_max=%u T_max=%u\n",
           (unsigned)sizeof storage,(unsigned)offsetof(PureStorage,legacy),
           (unsigned)sizeof storage.triple_particles,TRIPLE_BULLET_MAX,PURE_BULLET_MAX);
    for(unsigned i=0;i<4;i++){
        storage.before[i]=0xC200B000U+i;storage.after[i]=0xC200A000U+i;
    }
    for(unsigned count=2730U;count<=TRIPLE_BULLET_MAX;count+=91U){
        unsigned s9max=0U,gpumax=0U,frames=0U;int rc=0;
        triple_boss_init(storage.triple_particles,count,rays,PURE_SPEED_DEFAULT_Q8);
        sentinels();
        for(frames=0;frames<4096U;frames++){
            PureBulletBuildResult built;
            rc=triple_boss_build(storage.triple_particles,desc,&storage.legacy[0].dst,
                                count,0x400000U,rays,PURE_SPEED_DEFAULT_Q8,&built);
            sentinels();
            if(rc){CHECK(rc==-3);break;}
            CHECK(built.visible==count && built.changed==count);
            CHECK(built.s9_count<=2048U);
            unsigned gpu=0U;
            for(unsigned tile=0;tile<27U;tile++){
                CHECK(built.legacy_count[tile]<=256U);gpu+=built.legacy_count[tile];
            }
            CHECK(gpu+built.s9_count==count);
            if(built.s9_count>s9max)s9max=built.s9_count;
            if(gpu>gpumax)gpumax=gpu;
        }
        /* These three generations are established in-bounds model cases.
         * 3003 can hit the independent2048 descriptor limit; it must remain
         * visible as a bounded capacity result in the machine-readable log. */
        if(count<=2912U)CHECK(rc==0 && frames==4096U);
        printf("CAPACITY count=%u frames=%u rc=%d S9max=%u GPUmax=%u guard=1\n",
               count,frames,rc,s9max,gpumax);
    }

    /* B deliberately borrows T's plan bytes. Switching back must reconstruct
     * the actual T plan and every used particle; compare to clean storage. */
    const unsigned count=2093U;
    pressure_actual_rebuild(storage.plans,count);
    pressure_actual_rebuild(reference_plans,count);
    pure_bullet_test_model_init(storage.particles,count,storage.plans,rays,PURE_SPEED_DEFAULT_Q8);
    pure_bullet_test_model_init(reference_particles,count,reference_plans,rays,PURE_SPEED_DEFAULT_Q8);
    for(unsigned frame=0;frame<256U;frame++){
        PureBulletBuildResult actual,reference;
        CHECK(!pure_bullet_test_model_build(storage.particles,desc,&storage.legacy[0].dst,
                  count,0x400000U,storage.plans,rays,PURE_SPEED_DEFAULT_Q8,&actual));
        CHECK(!pure_bullet_test_model_build(reference_particles,reference_desc,reference_legacy,
                  count,0x400000U,reference_plans,rays,PURE_SPEED_DEFAULT_Q8,&reference));
        CHECK(!memcmp(&actual,&reference,sizeof actual));
        CHECK(!memcmp(desc,reference_desc,actual.s9_count*sizeof desc[0]));
        CHECK(!memcmp(storage.particles,reference_particles,count*sizeof reference_particles[0]));
        CHECK(!memcmp(storage.plans,reference_plans,count*sizeof reference_plans[0]));
        for(unsigned tile=0;tile<27U;tile++)
            CHECK(!memcmp(&storage.legacy[tile*256U].dst,reference_legacy+tile*256U,
                          actual.legacy_count[tile]*sizeof(uint32_t)));
        sentinels();
    }
    printf("PASS storage_union checks=%u T_restore_frames=256 no_board_performance_claim=1\n",checks);
    return 0;
}
