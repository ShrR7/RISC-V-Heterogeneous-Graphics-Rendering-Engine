/* SPDX-License-Identifier: MIT. Same parser is used by host tests and RV32. */
#include "package.h"
static uint32_t crc_byte(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for(unsigned b=0;b<8;b++)crc=(crc>>1)^((0U-(crc&1U))&UINT32_C(0xEDB88320));
    return crc;
}
uint32_t boot_crc32(const void *data, uint32_t bytes)
{
    const uint8_t *p=(const uint8_t *)data;uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<bytes;i++)crc=crc_byte(crc,p[i]);
    return ~crc;
}
uint32_t boot_header_crc32(const BootPackage *p)
{
    const uint8_t *b=(const uint8_t *)p;uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<BOOT_HEADER_BYTES;i++)crc=crc_byte(crc,(i>=60U&&i<64U)?0:b[i]);
    return ~crc;
}
static int span(uint32_t base,uint32_t bytes,uint32_t low,uint32_t end)
{return bytes!=0U && base>=low && base<end && bytes<=end-base;}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return a<b+bn && b<a+an;}
int boot_validate(const BootPackage *p,uint32_t expected_manifest_offset)
{
    static const uint32_t destination[4]={0x1000U,0x5C4000U,0x900000U,0x9B0000U};
    static const uint32_t limit[4]={0x20000U,0x67E000U,0x9B0000U,0x9C0000U};
    if(p->magic!=BOOT_MAGIC)return BOOT_EMAGIC;
    if(p->version!=BOOT_VERSION)return BOOT_EVERSION;
    if(p->header_bytes!=BOOT_HEADER_BYTES||p->section_count!=BOOT_SECTIONS)return BOOT_ECOUNT;
    if(boot_header_crc32(p)!=p->header_crc32)return BOOT_ECRC;
    if(p->entry!=0x1000U||p->flash_bytes!=0x800000U||p->manifest_offset!=expected_manifest_offset ||
       (p->manifest_offset&0xFFFU)||!span(p->manifest_offset,0x1000U,0x39F000U,p->flash_bytes)||
       p->bootstrap_offset!=BOOT_STAGE1_FLASH||p->bootstrap_bytes!=BOOT_STAGE1_BYTES)return BOOT_EHEADER;
    if(p->flags||p->reserved1)return BOOT_ERESERVED;
    if(p->jedec_id!=0xEF6017U)return BOOT_ECOMPAT;
    if(p->gpu_id!=0x47505531U||p->bul9_id!=0x42554C39U||p->bul9_revision!=0x10007U)return BOOT_ECOMPAT;
    for(unsigned i=0;i<BOOT_SECTIONS;i++){
        const BootSection *s=&p->section[i];
        if(s->kind!=i+1U)return BOOT_EKIND;
        if(s->flags||s->reserved0||s->reserved1)return BOOT_ERESERVED;
        if(s->ddr_address!=destination[i]||!span(s->ddr_address,s->bytes,destination[i],limit[i]))return BOOT_EDDR;
        if((s->flash_offset&0xFFFU)||!span(s->flash_offset,s->bytes,p->manifest_offset+0x1000U,p->flash_bytes))return BOOT_EFLASH;
        for(unsigned j=0;j<i;j++){
            const BootSection *t=&p->section[j];
            if(overlap(s->flash_offset,s->bytes,t->flash_offset,t->bytes)||
               overlap(s->ddr_address,s->bytes,t->ddr_address,t->bytes))return BOOT_EOVERLAP;
        }
    }
    return BOOT_OK;
}
