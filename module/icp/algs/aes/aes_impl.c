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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/icp.h>
#include <sys/crypto/spi.h>
#include <sys/simd.h>
#include <modes/modes.h>
#include <aes/aes_impl.h>

/*
 * Initialize AES encryption and decryption key schedules for the given
 * implementation.
 */
static void
aes_init_keysched_ops(const aes_impl_ops_t *ops, const uint8_t *cipherKey,
    uint_t keyBits, void *keysched)
{
	aes_key_t *newbie = keysched;
	uint_t keysize, i, j;
	union {
		uint64_t	ka64[4];
		uint32_t	ka32[8];
	} keyarr;

	switch (keyBits) {
	case 128:
		newbie->nr = 10;
		break;

	case 192:
		newbie->nr = 12;
		break;

	case 256:
		newbie->nr = 14;
		break;

	default:
		/* should never get here */
		return;
	}
	keysize = CRYPTO_BITS2BYTES(keyBits);

	/*
	 * Generic C implementation requires byteswap for little endian
	 * machines, various accelerated implementations for various
	 * architectures may not.
	 */
	if (!ops->needs_byteswap) {
		/* no byteswap needed */
		if (IS_P2ALIGNED(cipherKey, sizeof (uint64_t))) {
			for (i = 0, j = 0; j < keysize; i++, j += 8) {
				/* LINTED: pointer alignment */
				keyarr.ka64[i] = *((uint64_t *)&cipherKey[j]);
			}
		} else {
			memcpy(keyarr.ka32, cipherKey, keysize);
		}
	} else {
		/* byte swap */
		for (i = 0, j = 0; j < keysize; i++, j += 4) {
			keyarr.ka32[i] =
			    htonl(*(uint32_t *)(void *)&cipherKey[j]);
		}
	}

	ops->generate(newbie, keyarr.ka32, keyBits);
	newbie->ops = ops;

	/*
	 * Note: if there are systems that need the AES_64BIT_KS type in the
	 * future, move setting key schedule type to individual implementations
	 */
	newbie->type = AES_32BIT_KS;
}

/*
 * Initialize AES encryption and decryption key schedules.
 *
 * Parameters:
 * cipherKey	User key
 * keyBits	AES key size (128, 192, or 256 bits)
 * keysched	AES key schedule to be initialized, of type aes_key_t.
 *		Allocated by aes_alloc_keysched().
 */
void
aes_init_keysched(const uint8_t *cipherKey, uint_t keyBits, void *keysched)
{
	aes_init_keysched_ops(aes_impl_get_ops(), cipherKey, keyBits, keysched);
}


/*
 * Encrypt one block using AES.
 * Align if needed and (for x86 32-bit only) byte-swap.
 *
 * Parameters:
 * ks	Key schedule, of type aes_key_t
 * pt	Input block (plain text)
 * ct	Output block (crypto text).  Can overlap with pt
 */
int
aes_encrypt_block(const void *ks, const uint8_t *pt, uint8_t *ct)
{
	aes_key_t	*ksch = (aes_key_t *)ks;
	const aes_impl_ops_t	*ops = ksch->ops;

	if (IS_P2ALIGNED2(pt, ct, sizeof (uint32_t)) && !ops->needs_byteswap) {
		/* LINTED:  pointer alignment */
		ops->encrypt(&ksch->encr_ks.ks32[0], ksch->nr,
		    /* LINTED:  pointer alignment */
		    (uint32_t *)pt, (uint32_t *)ct);
	} else {
		uint32_t buffer[AES_BLOCK_LEN / sizeof (uint32_t)];

		/* Copy input block into buffer */
		if (ops->needs_byteswap) {
			buffer[0] = htonl(*(uint32_t *)(void *)&pt[0]);
			buffer[1] = htonl(*(uint32_t *)(void *)&pt[4]);
			buffer[2] = htonl(*(uint32_t *)(void *)&pt[8]);
			buffer[3] = htonl(*(uint32_t *)(void *)&pt[12]);
		} else
			memcpy(&buffer, pt, AES_BLOCK_LEN);

		ops->encrypt(&ksch->encr_ks.ks32[0], ksch->nr, buffer, buffer);

		/* Copy result from buffer to output block */
		if (ops->needs_byteswap) {
			*(uint32_t *)(void *)&ct[0] = htonl(buffer[0]);
			*(uint32_t *)(void *)&ct[4] = htonl(buffer[1]);
			*(uint32_t *)(void *)&ct[8] = htonl(buffer[2]);
			*(uint32_t *)(void *)&ct[12] = htonl(buffer[3]);
		} else
			memcpy(ct, &buffer, AES_BLOCK_LEN);
	}
	return (CRYPTO_SUCCESS);
}


/*
 * Decrypt one block using AES.
 * Align and byte-swap if needed.
 *
 * Parameters:
 * ks	Key schedule, of type aes_key_t
 * ct	Input block (crypto text)
 * pt	Output block (plain text). Can overlap with pt
 */
int
aes_decrypt_block(const void *ks, const uint8_t *ct, uint8_t *pt)
{
	aes_key_t	*ksch = (aes_key_t *)ks;
	const aes_impl_ops_t	*ops = ksch->ops;

	if (IS_P2ALIGNED2(ct, pt, sizeof (uint32_t)) && !ops->needs_byteswap) {
		/* LINTED:  pointer alignment */
		ops->decrypt(&ksch->decr_ks.ks32[0], ksch->nr,
		    /* LINTED:  pointer alignment */
		    (uint32_t *)ct, (uint32_t *)pt);
	} else {
		uint32_t buffer[AES_BLOCK_LEN / sizeof (uint32_t)];

		/* Copy input block into buffer */
		if (ops->needs_byteswap) {
			buffer[0] = htonl(*(uint32_t *)(void *)&ct[0]);
			buffer[1] = htonl(*(uint32_t *)(void *)&ct[4]);
			buffer[2] = htonl(*(uint32_t *)(void *)&ct[8]);
			buffer[3] = htonl(*(uint32_t *)(void *)&ct[12]);
		} else
			memcpy(&buffer, ct, AES_BLOCK_LEN);

		ops->decrypt(&ksch->decr_ks.ks32[0], ksch->nr, buffer, buffer);

		/* Copy result from buffer to output block */
		if (ops->needs_byteswap) {
			*(uint32_t *)(void *)&pt[0] = htonl(buffer[0]);
			*(uint32_t *)(void *)&pt[4] = htonl(buffer[1]);
			*(uint32_t *)(void *)&pt[8] = htonl(buffer[2]);
			*(uint32_t *)(void *)&pt[12] = htonl(buffer[3]);
		} else
			memcpy(pt, &buffer, AES_BLOCK_LEN);
	}
	return (CRYPTO_SUCCESS);
}


/*
 * Allocate key schedule for AES.
 *
 * Return the pointer and set size to the number of bytes allocated.
 * Memory allocated must be freed by the caller when done.
 *
 * Parameters:
 * size		Size of key schedule allocated, in bytes
 * kmflag	Flag passed to kmem_alloc(9F); ignored in userland.
 */
void *
aes_alloc_keysched(size_t *size, int kmflag)
{
	aes_key_t *keysched;

	keysched = kmem_alloc(sizeof (aes_key_t), kmflag);
	if (keysched != NULL) {
		*size = sizeof (aes_key_t);
		return (keysched);
	}
	return (NULL);
}

/* AES implementation that contains the fastest methods */
static aes_impl_ops_t aes_fastest_impl = {
	.name = "fastest"
};

/* All compiled in implementations */
static const aes_impl_ops_t *aes_all_impl[] = {
	&aes_generic_impl,
#if defined(__x86_64)
	&aes_x86_64_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(AES)
	&aes_aesni_impl,
#endif
};

/* Indicate that benchmark has been completed */
static boolean_t aes_impl_initialized = B_FALSE;

/* Select aes implementation */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX-1)

#define	AES_IMPL_READ(i) (*(volatile uint32_t *) &(i))

static uint32_t icp_aes_impl = IMPL_FASTEST;
static uint32_t user_sel_impl = IMPL_FASTEST;

/* Hold all supported implementations */
static size_t aes_supp_impl_cnt = 0;
static aes_impl_ops_t *aes_supp_impl[ARRAY_SIZE(aes_all_impl)];

/*
 * Returns the AES operations for encrypt/decrypt/key setup.  When a
 * SIMD implementation is not allowed in the current context, then
 * fallback to the fastest generic implementation.
 */
const aes_impl_ops_t *
aes_impl_get_ops(void)
{
	if (!kfpu_allowed())
		return (&aes_generic_impl);

	const aes_impl_ops_t *ops = NULL;
	const uint32_t impl = AES_IMPL_READ(icp_aes_impl);

	switch (impl) {
	case IMPL_FASTEST:
		ASSERT(aes_impl_initialized);
		ops = &aes_fastest_impl;
		break;
	case IMPL_CYCLE:
		/* Cycle through supported implementations */
		ASSERT(aes_impl_initialized);
		ASSERT3U(aes_supp_impl_cnt, >, 0);
		static size_t cycle_impl_idx = 0;
		size_t idx = (++cycle_impl_idx) % aes_supp_impl_cnt;
		ops = aes_supp_impl[idx];
		break;
	default:
		ASSERT3U(impl, <, aes_supp_impl_cnt);
		ASSERT3U(aes_supp_impl_cnt, >, 0);
		if (impl < ARRAY_SIZE(aes_all_impl))
			ops = aes_supp_impl[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

/*
 * Known answers from Appendix C.1 to C.3 of FIPS 197 (November 2001): the
 * same plaintext under 128, 192 and 256-bit keys of 00 01 02 ...
 */
static const uint8_t aes_selftest_pt[AES_BLOCK_LEN] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};

static const uint8_t aes_selftest_ct[3][AES_BLOCK_LEN] = {
	{
		0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
		0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
	},
	{
		0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0,
		0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91
	},
	{
		0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
		0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89
	}
};

/*
 * Check an implementation's key expansion, encryption and decryption
 * against known answers. Returns B_TRUE if it passes.
 */
boolean_t
aes_impl_selftest(const aes_impl_ops_t *ops)
{
	uint8_t key[32], ct[AES_BLOCK_LEN], pt[AES_BLOCK_LEN];
	aes_key_t *ks;
	boolean_t ok = B_TRUE;

	for (int i = 0; i < sizeof (key); i++)
		key[i] = i;

	ks = kmem_alloc(sizeof (*ks), KM_SLEEP);
	for (int i = 0; i < ARRAY_SIZE(aes_selftest_ct); i++) {
		aes_init_keysched_ops(ops, key, 128 + 64 * i, ks);
		(void) aes_encrypt_block(ks, aes_selftest_pt, ct);
		(void) aes_decrypt_block(ks, ct, pt);
		if (memcmp(ct, aes_selftest_ct[i], AES_BLOCK_LEN) != 0 ||
		    memcmp(pt, aes_selftest_pt, AES_BLOCK_LEN) != 0)
			ok = B_FALSE;
	}
	memset(ks, 0, sizeof (*ks));
	kmem_free(ks, sizeof (*ks));

	return (ok);
}

/*
 * Initialize all supported implementations.
 */
void
aes_impl_init(void)
{
	aes_impl_ops_t *curr_impl;
	int i, c;

	/*
	 * Move supported implementations into aes_supp_impls, leaving out any
	 * that compute wrong answers on this CPU. The generic implementation
	 * comes first and is kept regardless, as there is nothing to fall
	 * back to.
	 */
	for (i = 0, c = 0; i < ARRAY_SIZE(aes_all_impl); i++) {
		curr_impl = (aes_impl_ops_t *)aes_all_impl[i];

		if (!curr_impl->is_supported())
			continue;
		if (!aes_impl_selftest(curr_impl)) {
			cmn_err(CE_WARN, "ICP: the %s implementation of aes "
			    "failed its self-test, and %s", curr_impl->name,
			    c == 0 ? "can't be disabled" : "has been disabled");
			if (c != 0)
				continue;
		}
		aes_supp_impl[c++] = curr_impl;
	}
	aes_supp_impl_cnt = c;

	/*
	 * Set the fastest implementation given the assumption that the
	 * hardware accelerated version is the fastest. Implementations are
	 * listed slowest first.
	 */
	memcpy(&aes_fastest_impl, aes_supp_impl[aes_supp_impl_cnt - 1],
	    sizeof (aes_fastest_impl));

	strlcpy(aes_fastest_impl.name, "fastest", AES_IMPL_NAME_MAX);

	/* Finish initialization */
	atomic_swap_32(&icp_aes_impl, user_sel_impl);
	aes_impl_initialized = B_TRUE;
}

static const struct {
	const char *name;
	uint32_t sel;
} aes_impl_opts[] = {
		{ "cycle",	IMPL_CYCLE },
		{ "fastest",	IMPL_FASTEST },
};

/*
 * Function sets desired aes implementation.
 *
 * If we are called before init(), user preference will be saved in
 * user_sel_impl, and applied in later init() call. This occurs when module
 * parameter is specified on module load. Otherwise, directly update
 * icp_aes_impl.
 *
 * @val		Name of aes implementation to use
 * @param	Unused.
 */
int
aes_impl_set(const char *val)
{
	int err = -EINVAL;
	char req_name[AES_IMPL_NAME_MAX];
	uint32_t impl = AES_IMPL_READ(user_sel_impl);
	size_t i;

	/* sanitize input */
	i = strnlen(val, AES_IMPL_NAME_MAX);
	if (i == 0 || i >= AES_IMPL_NAME_MAX)
		return (err);

	strlcpy(req_name, val, AES_IMPL_NAME_MAX);
	while (i > 0 && isspace(req_name[i-1]))
		i--;
	req_name[i] = '\0';

	/* Check mandatory options */
	for (i = 0; i < ARRAY_SIZE(aes_impl_opts); i++) {
		if (strcmp(req_name, aes_impl_opts[i].name) == 0) {
			impl = aes_impl_opts[i].sel;
			err = 0;
			break;
		}
	}

	/* check all supported impl if init() was already called */
	if (err != 0 && aes_impl_initialized) {
		/* check all supported implementations */
		for (i = 0; i < aes_supp_impl_cnt; i++) {
			if (strcmp(req_name, aes_supp_impl[i]->name) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	if (err == 0) {
		if (aes_impl_initialized)
			atomic_swap_32(&icp_aes_impl, impl);
		else
			atomic_swap_32(&user_sel_impl, impl);
	}

	return (err);
}

#if defined(_KERNEL) && defined(__linux__)

static int
icp_aes_impl_set(const char *val, zfs_kernel_param_t *kp)
{
	return (aes_impl_set(val));
}

static int
icp_aes_impl_get(char *buffer, zfs_kernel_param_t *kp)
{
	int i, cnt = 0;
	char *fmt;
	const uint32_t impl = AES_IMPL_READ(icp_aes_impl);

	/* list mandatory options */
	for (i = 0; i < ARRAY_SIZE(aes_impl_opts); i++) {
		fmt = (impl == aes_impl_opts[i].sel) ? "[%s] " : "%s ";
		cnt += kmem_scnprintf(buffer + cnt, PAGE_SIZE - cnt, fmt,
		    aes_impl_opts[i].name);
	}

	/* list all supported implementations */
	for (i = 0; i < aes_supp_impl_cnt; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += kmem_scnprintf(buffer + cnt, PAGE_SIZE - cnt, fmt,
		    aes_supp_impl[i]->name);
	}

	return (cnt);
}

module_param_call(icp_aes_impl, icp_aes_impl_set, icp_aes_impl_get,
    NULL, 0644);
MODULE_PARM_DESC(icp_aes_impl, "Select aes implementation.");
#endif
