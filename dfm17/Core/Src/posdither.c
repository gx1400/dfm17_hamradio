/**
  ******************************************************************************
  * @file    posdither.c
  * @brief   fox mode: report a randomly offset position, the area rather than
  *          the spot (#72)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Derek Rowland <gx1400@gmail.com>
  * All rights reserved.
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  ******************************************************************************
  * Every position report gets a fresh offset, uniform over a disc of radius R
  * centred on the box: hunters learn the area, not the spot. R is fixed, or
  * shrinks linearly from pos_dither_m to pos_dither_min_m (0: the real spot)
  * over pos_dither_minutes, so the hunt gets easier the longer it runs.
  *
  * Integer maths only (no doubles, no trig): the point is drawn by rejection
  * from the square around the disc, which is exactly uniform over the disc, and
  * the longitude scale comes from a 1-degree cosine table.
  ******************************************************************************
  */

#include "posdither.h"

/* metres per degree of latitude (and of longitude at the equator) */
#define M_PER_DEG 111320LL
#define E7		  10000000LL

static uint32_t state = 0x9E3779B9UL;

void posdither_seed(uint32_t seed) {
	state = seed ? seed : 0x9E3779B9UL; /* xorshift needs a non-zero state */
}

/* mixes more entropy in: the low bits of a PPS interval, an ADC reading */
void posdither_stir(uint32_t x) {
	state ^= x * 0x85EBCA6BUL;
	state = (state << 13 | state >> 19) * 5 + 0xE6546B64UL;
	if (!state)
		state = 0x9E3779B9UL;
}

/* xorshift32 */
uint32_t posdither_rand(void) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

/* 0 .. n-1, n <= 2^32 - 1 */
static uint32_t rand_below(uint32_t n) {
	return (uint32_t)(((uint64_t)posdither_rand() * n) >> 32);
}

uint32_t posdither_radius_m(const settings_t *s, uint32_t ms) {
	if (s->pos_dither_mode == POS_DITHER_FIXED)
		return s->pos_dither_m;
	if (s->pos_dither_mode != POS_DITHER_SHRINK)
		return 0;
	uint64_t span_ms = (uint64_t)s->pos_dither_minutes * 60000;
	if (ms >= span_ms)
		return s->pos_dither_min_m;
	uint32_t shrink = s->pos_dither_m - s->pos_dither_min_m;
	return s->pos_dither_m - (uint32_t)((uint64_t)shrink * ms / span_ms);
}

/* cos(d degrees) * 65535, d = 0..90 */
static const uint16_t cos_deg[91] = {
		65535,
		65525,
		65495,
		65445,
		65375,
		65286,
		65176,
		65047,
		64897,
		64728,
		64539,
		64331,
		64103,
		63855,
		63588,
		63302,
		62996,
		62671,
		62327,
		61965,
		61583,
		61182,
		60763,
		60325,
		59869,
		59395,
		58902,
		58392,
		57864,
		57318,
		56755,
		56174,
		55577,
		54962,
		54331,
		53683,
		53019,
		52339,
		51642,
		50930,
		50203,
		49460,
		48702,
		47929,
		47142,
		46340,
		45524,
		44695,
		43851,
		42995,
		42125,
		41243,
		40347,
		39440,
		38521,
		37589,
		36647,
		35693,
		34728,
		33753,
		32768,
		31772,
		30767,
		29752,
		28729,
		27696,
		26655,
		25607,
		24550,
		23486,
		22414,
		21336,
		20251,
		19161,
		18064,
		16962,
		15854,
		14742,
		13625,
		12505,
		11380,
		10252,
		9121,
		7987,
		6850,
		5712,
		4571,
		3430,
		2287,
		1144,
		0,
};

uint32_t posdither_cos_q16(int32_t lat_e7) {
	uint32_t a = lat_e7 < 0 ? (uint32_t)(-(int64_t)lat_e7) : (uint32_t)lat_e7;
	uint32_t deg = a / E7;
	if (deg >= 89)
		return cos_deg[89];
	uint32_t frac = a % E7; /* linear between whole degrees */
	return cos_deg[deg] - (uint32_t)((uint64_t)(cos_deg[deg] - cos_deg[deg + 1]) * frac / E7);
}

/* a point uniform over the disc of radius r m: by rejection from the square
 * (accepts pi/4 of draws); gives up on (0, 0) after 32 tries, never in practice */
static void disc_point(uint32_t r, int32_t *x, int32_t *y) {
	for (int i = 0; i < 32; i++) {
		int32_t a = (int32_t)rand_below(2 * r + 1) - (int32_t)r;
		int32_t b = (int32_t)rand_below(2 * r + 1) - (int32_t)r;
		if ((int64_t)a * a + (int64_t)b * b <= (int64_t)r * r) {
			*x = a;
			*y = b;
			return;
		}
	}
	*x = *y = 0;
}

static int64_t div_round(int64_t n, int64_t d) {
	return n >= 0 ? (n + d / 2) / d : (n - d / 2) / d;
}

uint8_t posdither_apply(
		const settings_t *s, uint32_t ms, int32_t *lat_e7, int32_t *lon_e7, int32_t *hmsl_mm) {
	if (s->pos_dither_mode != POS_DITHER_FIXED && s->pos_dither_mode != POS_DITHER_SHRINK)
		return 0;
	posdither_stir(ms);

	int32_t east = 0, north = 0;
	uint32_t r = posdither_radius_m(s, ms);
	if (r)
		disc_point(r, &east, &north);

	/* north: metres to 1e-7 deg, held inside +-90 deg */
	int64_t lat = *lat_e7 + div_round((int64_t)north * E7, M_PER_DEG);
	if (lat > 90 * E7)
		lat = 90 * E7;
	if (lat < -90 * E7)
		lat = -90 * E7;

	/* east: scaled by 1/cos(lat) at the true position, wrapped at +-180 deg */
	uint32_t c = posdither_cos_q16(*lat_e7);
	int64_t lon = *lon_e7 + div_round((int64_t)east * E7 * 65535, M_PER_DEG * c);
	if (lon > 180 * E7)
		lon -= 360 * E7;
	if (lon <= -180 * E7)
		lon += 360 * E7;

	*lat_e7 = (int32_t)lat;
	*lon_e7 = (int32_t)lon;

	uint32_t a = s->pos_dither_alt_m;
	if (a)
		*hmsl_mm += ((int32_t)rand_below(2 * a + 1) - (int32_t)a) * 1000;
	return 1;
}
