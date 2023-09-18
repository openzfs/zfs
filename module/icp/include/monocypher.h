// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2017-2019, Loup Vaillant
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Monocypher 4.0.2 (Poly1305, Chacha20, and supporting utilities)
 * adapted for OpenZFS by Rob Norris <robn@despairlabs.com>
 */

/*
 * Note: this follows the Monocypher style rather than the OpenZFS style to
 * keep the diff to the bare minimum. This is important for making it easy to
 * compare the two and confirm that they are in fact the same. The diff should
 * be almost entirely in deleted lines.
 */

#ifndef MONOCYPHER_H
#define MONOCYPHER_H

#include <sys/types.h>


// Constant time comparisons
// -------------------------

// Return 0 if a and b are equal, -1 otherwise
int crypto_verify16(const uint8_t a[16], const uint8_t b[16]);

// Erase sensitive data
// --------------------
void crypto_wipe(void *secret, size_t size);


// Chacha20
// --------

// Unauthenticated stream cipher.
// Don't forget to add authentication.
uint32_t crypto_chacha20_ietf(uint8_t       *cipher_text,
                              const uint8_t *plain_text,
                              size_t         text_size,
                              const uint8_t  key[32],
                              const uint8_t  nonce[12],
                              uint32_t       ctr);


// Poly 1305
// ---------

// This is a *one time* authenticator.
// Disclosing the mac reveals the key.

// Incremental interface
typedef struct {
	// Do not rely on the size or contents of this type,
	// for they may change without notice.
	uint8_t  c[16];  // chunk of the message
	size_t   c_idx;  // How many bytes are there in the chunk.
	uint32_t r  [4]; // constant multiplier (from the secret key)
	uint32_t pad[4]; // random number added at the end (from the secret key)
	uint32_t h  [5]; // accumulated hash
} crypto_poly1305_ctx;

void crypto_poly1305_init  (crypto_poly1305_ctx *ctx, const uint8_t key[32]);
void crypto_poly1305_update(crypto_poly1305_ctx *ctx,
                            const uint8_t *message, size_t message_size);
void crypto_poly1305_final (crypto_poly1305_ctx *ctx, uint8_t mac[16]);

#endif /* MONOCYPHER_H */
