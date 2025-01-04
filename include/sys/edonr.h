// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Based on Edon-R implementation for SUPERCOP, based on NIST API.
 * Copyright (c) 2009, 2010 Jørn Amundsen <jorn.amundsen@ntnu.no>
 * Copyright (c) 2013 Saso Kiselkov, All rights reserved
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	_SYS_EDONR_H_
#define	_SYS_EDONR_H_

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef  _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

/*
 * EdonR allows to call EdonRUpdate() consecutively only if the total length
 * of stored unprocessed data and the new supplied data is less than or equal
 * to the BLOCK_SIZE on which the compression functions operates.
 * Otherwise an assertion failure is invoked.
 */

/* Specific algorithm definitions */
#define	EdonR512_DIGEST_SIZE	64
#define	EdonR512_BLOCK_SIZE	128
#define	EdonR512_BLOCK_BITSIZE	1024

typedef struct {
	uint64_t DoublePipe[16];
	uint8_t LastPart[EdonR512_BLOCK_SIZE * 2];
} EdonRData512;

typedef struct {
	uint64_t bits_processed;
	int unprocessed_bits;
	union {
		EdonRData512 p512[1];
	} pipe[1];
} EdonRState;

void EdonRInit(EdonRState *state);
void EdonRUpdate(EdonRState *state, const uint8_t *data, size_t databitlen);
void EdonRFinal(EdonRState *state, uint8_t *hashval);
void EdonRHash(const uint8_t *data, size_t databitlen, uint8_t *hashval);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EDONR_H_ */
