// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
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
#define	gcm_ghash_vpclmulqdq_avx2	icp_gcm_ghash_vpclmulqdq_avx2
#define	aes_gcm_enc_update_vaes_avx2	icp_aes_gcm_enc_update_vaes_avx2
#define	aes_gcm_dec_update_vaes_avx2	icp_aes_gcm_dec_update_vaes_avx2
