/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compile the original H and candidate sources as separate translation units
 * with renamed public model functions. Compare byte-for-byte, including
 * descriptor/legacy-list order and unwritten sentinels on every frame. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pure_bullet_test.h"

void baseline_model_init(PureBulletParticle *,unsigned,const PureBulletPlan *,const PureBulletRayMeta *,unsigned);
void candidate_model_init(PureBulletParticle *,unsigned,const PureBulletPlan *,const PureBulletRayMeta *,unsigned);
int baseline_model_build(PureBulletParticle *,PureBulletDesc *,uint32_t *,unsigned,uint32_t,const PureBulletPlan *,const PureBulletRayMeta *,unsigned,PureBulletBuildResult *);
int candidate_model_build(PureBulletParticle *,PureBulletDesc *,uint32_t *,unsigned,uint32_t,const PureBulletPlan *,const PureBulletRayMeta *,unsigned,PureBulletBuildResult *);

typedef struct {
    PureBulletParticle particle[PURE_BULLET_STORAGE_CAPACITY];
    PureBulletDesc desc[2048];
    uint32_t legacy[PURE_SCATTER_TILE_COUNT*PURE_SCATTER_BUCKET];
    PureBulletBuildResult result;
} Outputs;

static Outputs base, cand;
static uint32_t crc_table[256];
static unsigned comparisons;

static void fail(const char *reason,unsigned count,unsigned speed,unsigned frame)
{
    fprintf(stderr,"FAIL %s count=%u speed=%u frame=%u\n",reason,count,speed,frame);
    exit(1);
}

static uint32_t crc_more(uint32_t crc,const void *data,size_t bytes)
{
    const uint8_t *p=(const uint8_t *)data;
    for(size_t i=0;i<bytes;i++)crc=crc_table[(crc^p[i])&255U]^(crc>>8);
    return crc;
}

static void equal(const void *a,const void *b,size_t bytes,const char *label,
                  unsigned count,unsigned speed,unsigned frame)
{
    comparisons++;
    if(memcmp(a,b,bytes))fail(label,count,speed,frame);
}

int main(int argc,char **argv)
{
    static const unsigned counts[]={728U,2002U,2093U,2730U};
    static const unsigned speeds[]={512U,612U,1024U};
    const unsigned frames=300U;
    if(argc!=2){fprintf(stderr,"usage: test_pure_model_equivalence scatter16.bin\n");return 2;}
    FILE *file=fopen(argv[1],"rb");
    if(!file){perror(argv[1]);return 2;}
    uint8_t *blob=malloc(PURE_SCATTER_BYTES);
    if(!blob || fread(blob,1,PURE_SCATTER_BYTES,file)!=PURE_SCATTER_BYTES || fgetc(file)!=EOF){
        fprintf(stderr,"bad scatter blob length\n");return 2;
    }
    fclose(file);
    for(unsigned i=0;i<256U;i++){
        uint32_t c=i;
        for(unsigned b=0;b<8U;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);
        crc_table[i]=c;
    }
    if((crc_more(UINT32_MAX,blob,PURE_SCATTER_BYTES)^UINT32_MAX)!=PURE_SCATTER_BLOB_CRC)
        fail("scatter CRC",0,0,0);
    const PureBulletScatterHeader *h=(const PureBulletScatterHeader *)blob;
    if(h->magic!=PURE_SCATTER_MAGIC || h->ray_count!=PURE_RAY_COUNT ||
       h->plan_count!=PURE_BULLET_MAX || h->ray_meta_offset!=PURE_SCATTER_RAY_OFFSET ||
       h->plan_offset!=PURE_SCATTER_PLAN_OFFSET)fail("scatter ABI",0,0,0);
    const PureBulletRayMeta *rays=(const PureBulletRayMeta *)(blob+h->ray_meta_offset);
    const PureBulletPlan *plans=(const PureBulletPlan *)(blob+h->plan_offset);
    for(unsigned i=0;i<h->plan_count;i++)
        if(plans[i].ray!=i%PURE_RAY_COUNT || plans[i].phase_slot!=i/PURE_RAY_COUNT+1U)
            fail("blob plan does not match firmware rebuild order",0,0,i);
    unsigned cases=0U;
    for(unsigned ci=0;ci<sizeof(counts)/sizeof(counts[0]);ci++){
        for(unsigned si=0;si<sizeof(speeds)/sizeof(speeds[0]);si++){
            unsigned count=counts[ci],speed=speeds[si];
            uint32_t crc=UINT32_MAX,respawns=0U;
            memset(&base,0xa5,sizeof base);memset(&cand,0xa5,sizeof cand);
            baseline_model_init(base.particle,count,plans,rays,speed);
            candidate_model_init(cand.particle,count,plans,rays,speed);
            equal(base.particle,cand.particle,sizeof base.particle,"init particles",count,speed,0U);
            for(unsigned frame=0;frame<frames;frame++){
                uint32_t fb=frame&1U?0x00300000U:0x00100000U;
                /* Re-poison every output list: comparison includes list tail
                 * and empty buckets, detecting extra/unordered stores too. */
                memset(base.desc,0xa5,sizeof base.desc);memset(cand.desc,0xa5,sizeof cand.desc);
                memset(base.legacy,0xa5,sizeof base.legacy);memset(cand.legacy,0xa5,sizeof cand.legacy);
                int rb=baseline_model_build(base.particle,base.desc,base.legacy,count,fb,plans,rays,speed,&base.result);
                int rc=candidate_model_build(cand.particle,cand.desc,cand.legacy,count,fb,plans,rays,speed,&cand.result);
                if(rb || rc || rb!=rc)fail("build return",count,speed,frame);
                equal(base.particle,cand.particle,sizeof base.particle,"particles",count,speed,frame);
                equal(base.desc,cand.desc,sizeof base.desc,"ordered Stage9 descriptors",count,speed,frame);
                equal(base.legacy,cand.legacy,sizeof base.legacy,"ordered legacy destinations",count,speed,frame);
                equal(&base.result,&cand.result,sizeof base.result,"result and checksum",count,speed,frame);
                if(base.result.changed!=count || base.result.visible!=count)fail("count conservation",count,speed,frame);
                unsigned total=base.result.s9_count;
                for(unsigned t=0;t<PURE_SCATTER_TILE_COUNT;t++)total+=base.result.legacy_count[t];
                if(total!=count)fail("route conservation",count,speed,frame);
                respawns+=base.result.respawn;
                crc=crc_more(crc,base.particle,sizeof base.particle);
                crc=crc_more(crc,base.desc,sizeof base.desc);
                crc=crc_more(crc,base.legacy,sizeof base.legacy);
                crc=crc_more(crc,&base.result,sizeof base.result);
            }
            if(!respawns)fail("no edge-respawn coverage",count,speed,frames);
            printf("PASS count=%u speed_q8=%u frames=%u ordered_bytes_crc32=%08x natural_respawns=%u\n",
                   count,speed,frames,crc^UINT32_MAX,respawns);
            cases++;
        }
    }
    printf("PASS H vs candidate model equivalence: %u cases x %u frames = %u updates; %u full-buffer comparisons; scatter_crc=%08x\n",
           cases,frames,cases*frames,comparisons,PURE_SCATTER_BLOB_CRC);
    free(blob);
    return 0;
}
