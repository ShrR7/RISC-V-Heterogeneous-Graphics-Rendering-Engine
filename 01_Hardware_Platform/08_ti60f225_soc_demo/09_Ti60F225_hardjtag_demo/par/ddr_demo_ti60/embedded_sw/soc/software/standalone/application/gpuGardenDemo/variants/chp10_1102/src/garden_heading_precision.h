/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integer presentation heading. No Garden state, asset, MMIO or collision writes.
 */
#ifndef GARDEN_HEADING_PRECISION_H
#define GARDEN_HEADING_PRECISION_H
#include <stdint.h>
/* Garden convention: 0=east,256=south,512=west,768=north.
 * Return nearest32/64 direction as a1024-turn angle (step32/16).
 * Zero velocity uses fallback modulo1024, nearest step; exact fallback
 * halfway ties choose increasing angle. Nonzero Q30 boundary ties choose
 * the higher first-octant bin before reflection.
 * Max angular error: nominal5.625/2.8125deg plus <=2.668043e-8deg from
 * nearest-integer Q30 tangent thresholds. This epsilon must not be hidden.
 * Runtime uses only unsigned magnitude/shift/multiply/comparison, at most
 * three/four binary-search threshold comparisons, no floating point or division.
 */
uint16_t heading32_from_velocity(int32_t vx,int32_t vy,uint32_t fallback_angle);
uint16_t heading64_from_velocity(int32_t vx,int32_t vy,uint32_t fallback_angle);
/* Exact predicate for a candidate0..1008 in steps16. It agrees with
 * heading64_from_velocity==candidate, including all Q30 and fallback ties;
 * zero/one/two threshold comparisons, no state, division or approximation. */
unsigned heading64_matches(int32_t vx,int32_t vy,uint32_t fallback_angle,unsigned candidate_angle);
#endif
