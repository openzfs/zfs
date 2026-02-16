// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2026 Attila Fülöp <attila@fueloep.org>
 */

/*
 * Prepend `icp_` to each function name defined in gcm assembly files.
 * This avoids potential symbol conflicts with linux libcrypto in case of
 * in-tree compilation. To keep the diff noise low, we do this using macros.
 *
 * Currently only done for aesni-gcm-avx2-vaes.S since there is a real conflict.
 */

/* module/icp/asm-x86_64/modes/aesni-gcm-avx2-vaes.S */
#define	gcm_init_vpclmulqdq_avx2	icp_gcm_init_vpclmulqdq_avx2
#define	gcm_gmult_vpclmulqdq_avx2	icp_gcm_gmult_vpclmulqdq_avx2
#define	gcm_ghash_vpclmulqdq_avx2	icp_gcm_ghash_vpclmulqdq_avx2
#define	aes_gcm_enc_update_vaes_avx2	icp_aes_gcm_enc_update_vaes_avx2
#define	aes_gcm_dec_update_vaes_avx2	icp_aes_gcm_dec_update_vaes_avx2
