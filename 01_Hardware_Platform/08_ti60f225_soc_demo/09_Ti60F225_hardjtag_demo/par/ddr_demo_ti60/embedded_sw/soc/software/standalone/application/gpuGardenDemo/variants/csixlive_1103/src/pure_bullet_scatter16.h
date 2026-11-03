/* Generated91-ray/unitQ14 radial ABI; canonical27game tiles unchanged. */
#ifndef PURE_BULLET_SCATTER16_H
#define PURE_BULLET_SCATTER16_H
#include <stdint.h>
#define PURE_SCATTER_BASE 0x009B0000U
#define PURE_SCATTER_MAGIC 0x32584D50U
#define PURE_SCATTER_BYTES 28640U
#define PURE_SCATTER_PAYLOAD_BYTES 28576U
#define PURE_SCATTER_PAYLOAD_CRC 0x9CCEC337U
#define PURE_SCATTER_BLOB_CRC 0x4EE686C0U
#define PURE_SCATTER_TILE_COUNT 27U
#define PURE_SCATTER_RAY_COUNT 91U
#define PURE_SCATTER_FAMILY_COUNT 7U
#define PURE_SCATTER_BUCKET 256U
#define PURE_SCATTER_META_OFFSET 64U
#define PURE_SCATTER_RAY_OFFSET 496U
#define PURE_SCATTER_PLAN_OFFSET 1952U
#define PURE_SCATTER_PIXEL_OFFSET 12872U
#define PURE_SCATTER_PHASE_SLOTS 30U
#define PURE_UNIFORM_LEGACY_ROWS 91U
#define PURE_FAST_LEGACY_ROWS 80U
typedef struct { uint32_t magic,revision,total_bytes,payload_bytes,payload_crc,tile_count,ray_count,plan_count,tile_meta_offset,ray_meta_offset,plan_offset,pixel_offset,strings_offset,strings_bytes,source_packed,phase_slots; } PureBulletScatterHeader;
typedef struct {uint32_t asset,packed,source_crc,tile_crc;} PureBulletTileMeta;
typedef struct {uint32_t velocity,packed,fixed_ttl,reserved;} PureBulletRayMeta;
typedef struct {uint16_t ray,phase_slot;} PureBulletPlan;
_Static_assert(sizeof(PureBulletScatterHeader)==64U,"header ABI");
_Static_assert(sizeof(PureBulletRayMeta)==16U,"ray ABI");
_Static_assert(sizeof(PureBulletTileMeta)==16U,"tile metadata ABI");
_Static_assert(sizeof(PureBulletPlan)==4U,"plan ABI");
#define PURE_SCATTER_TEXT_START ((const char *)(uintptr_t)(PURE_SCATTER_BASE+26696U))
#define PURE_SCATTER_TEXT_REPORT ((const char *)(uintptr_t)(PURE_SCATTER_BASE+27412U))
#define PURE_SCATTER_TEXT_WARMUP ((const char *)(uintptr_t)(PURE_SCATTER_BASE+27926U))
#define PURE_SCATTER_TEXT_PERIOD ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28110U))
#define PURE_SCATTER_TEXT_PREP ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28208U))
#define PURE_SCATTER_TEXT_FAIL ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28384U))
#define PURE_SCATTER_TEXT_EXIT ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28447U))
#define PURE_SCATTER_TEXT_LEFT ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28525U))
#define PURE_SCATTER_TEXT_FOOTER ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28554U))
#define PURE_SCATTER_TEXT_KEYS ((const char *)(uintptr_t)(PURE_SCATTER_BASE+28600U))
#endif
