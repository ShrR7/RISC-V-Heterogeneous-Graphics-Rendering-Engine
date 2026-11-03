#pragma once

#include <stdint.h>

/* GPU APB register window supplied by the generated SoC BSP. */
#ifdef IO_APB_SLAVE_0_INPUT
#define GPU_BASE IO_APB_SLAVE_0_INPUT
#else
#define GPU_BASE 0xF8100000U
#endif

#define GPU_CTRL       (*(volatile uint32_t *)(GPU_BASE + 0x00U))
#define GPU_STATUS     (*(volatile uint32_t *)(GPU_BASE + 0x04U))
#define GPU_SRC_ADDR   (*(volatile uint32_t *)(GPU_BASE + 0x08U))
#define GPU_DST_ADDR   (*(volatile uint32_t *)(GPU_BASE + 0x0CU))
#define GPU_WIDTH      (*(volatile uint32_t *)(GPU_BASE + 0x10U))
#define GPU_HEIGHT     (*(volatile uint32_t *)(GPU_BASE + 0x14U))
#define GPU_SRC_STRIDE (*(volatile uint32_t *)(GPU_BASE + 0x18U))
#define GPU_DST_STRIDE (*(volatile uint32_t *)(GPU_BASE + 0x1CU))
#define GPU_COLOR      (*(volatile uint32_t *)(GPU_BASE + 0x20U))
#define GPU_ID         (*(volatile uint32_t *)(GPU_BASE + 0x24U))
/* Stage 6 dual-buffer handshake. */
#define GPU_FB_CTRL    (*(volatile uint32_t *)(GPU_BASE + 0x28U))
#define GPU_FB_STATUS  (*(volatile uint32_t *)(GPU_BASE + 0x2CU))
/* PR #32 / interface_spec section 8.5: global Alpha, 0..32. */
#define GPU_ALPHA      (*(volatile uint32_t *)(GPU_BASE + 0x30U))

#define GPU_MAGIC_ID 0x47505531U

/* CTRL.start=1 with CTRL[2:1] = opcode. */
#define GPU_CTRL_FILL    0x00000001U
#define GPU_CTRL_COPY    0x00000003U
#define GPU_CTRL_BITBLT  0x00000005U
#define GPU_CTRL_ALPHA_ENABLE 0x00000100U
#define GPU_CTRL_ALPHA_BITBLT (GPU_CTRL_BITBLT | GPU_CTRL_ALPHA_ENABLE)

#define STATUS_BUSY  0x00000001U
#define STATUS_DONE  0x00000002U
#define STATUS_ERROR 0x00000004U

/* FB_STATUS bit fields. */
#define FB_STATUS_ACTIVE_MASK  0x00000001U
#define FB_STATUS_PENDING_MASK 0x00000002U

/* Stage 6 framebuffer contract: 1280x720 RGB565, stride 2560 B. */
#define DISPLAY_WIDTH         1280U
#define DISPLAY_HEIGHT        720U
#define DISPLAY_STRIDE_BYTES  2560U
#define DISPLAY_STRIDE_PIXELS (DISPLAY_STRIDE_BYTES / 2U)
#define FB0_ADDR              0x00200000U
#define FB1_ADDR              0x00400000U
#define FB_SIZE_BYTES         1843200U

/*
 * Sprite / asset area, per Stage 6 plan §4.  The plan requires A to confirm
 * that 0x005C4000..0x00600000 does not collide with program, stack, heap, the
 * JTAG download area or BSP data before it is used for real assets.
 */
#define ASSET_BASE_ADDR       0x005C4000U
#define ASSET_SIZE_BYTES      0x0003C000U
