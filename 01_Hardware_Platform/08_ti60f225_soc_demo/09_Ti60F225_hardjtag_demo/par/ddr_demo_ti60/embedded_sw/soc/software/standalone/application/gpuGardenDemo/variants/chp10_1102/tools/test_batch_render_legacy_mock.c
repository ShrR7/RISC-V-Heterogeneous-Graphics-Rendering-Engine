/* SPDX-License-Identifier: GPL-3.0-or-later
 * Link-only shims for inherited SINGLE-mode independent pixel oracles.
 * That fixture never enables batching. New batch_render separately verifies
 * actual list submission and failure semantics; this file makes no such claim.
 */
#include "gpu_driver.h"
int gpu_busy(void){return 0;}
int gpu_fill_batch(const GpuFillEntry *p,uint32_t n,uint32_t ds,uint16_t c)
{for(unsigned i=0;i<n;i++){int rc=gpu_fill(p[i].dst,p[i].width,p[i].height,ds,c);if(rc!=GPU_OK)return rc;}return GPU_OK;}
int gpu_bitblt_batch(const GpuBlitEntry *p,uint32_t n,uint32_t s,uint32_t w,uint32_t h,uint32_t ss,uint32_t ds,uint16_t k,uint32_t a)
{for(unsigned i=0;i<n;i++){int rc=a==32U?gpu_bitblt(s,p[i].dst,w,h,ss,ds,k):gpu_alpha_bitblt(s,p[i].dst,w,h,ss,ds,k,a);if(rc!=GPU_OK)return rc;}return GPU_OK;}
