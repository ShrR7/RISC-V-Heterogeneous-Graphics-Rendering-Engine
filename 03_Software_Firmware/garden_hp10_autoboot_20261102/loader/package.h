/* SPDX-License-Identifier: MIT */
#ifndef GARDEN_BOOT_PACKAGE_H
#define GARDEN_BOOT_PACKAGE_H
#include <stdint.h>
#define BOOT_MAGIC UINT32_C(0x31424743) /* CGB1, little endian */
#define BOOT_VERSION 1U
#define BOOT_SECTIONS 4U
#define BOOT_HEADER_BYTES 192U
#define BOOT_STAGE1_FLASH UINT32_C(0x00380000)
#define BOOT_STAGE1_BYTES UINT32_C(0x0001F000)
#define BOOT_RELOC_BASE UINT32_C(0x00C00000)
#define BOOT_RELOC_END UINT32_C(0x00C20000)
typedef struct {
    uint32_t kind, flash_offset, ddr_address, bytes, crc32, flags, reserved0, reserved1;
} BootSection;
typedef struct {
    uint32_t magic, version, header_bytes, section_count;
    uint32_t entry, flash_bytes, manifest_offset, flags;
    uint32_t gpu_id, bul9_id, bul9_revision, bootstrap_offset;
    uint32_t bootstrap_bytes, jedec_id, reserved1, header_crc32;
    BootSection section[BOOT_SECTIONS];
} BootPackage;
_Static_assert(sizeof(BootSection)==32,"section ABI");
_Static_assert(sizeof(BootPackage)==BOOT_HEADER_BYTES,"package ABI");
enum {
    BOOT_OK=0, BOOT_EMAGIC=1, BOOT_EVERSION=2, BOOT_ECOUNT=3,
    BOOT_ECRC=4, BOOT_EHEADER=5, BOOT_ECOMPAT=6, BOOT_EKIND=7,
    BOOT_EDDR=8, BOOT_EFLASH=9, BOOT_EOVERLAP=10, BOOT_ERESERVED=11
};
uint32_t boot_crc32(const void *data, uint32_t bytes);
uint32_t boot_header_crc32(const BootPackage *p);
int boot_validate(const BootPackage *p, uint32_t expected_manifest_offset);
#endif
