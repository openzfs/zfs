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

/*
 * AES provider for the Kernel Cryptographic Framework (KCF)
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/icp.h>
#include <modes/modes.h>
#define	_AES_IMPL
#include <aes/aes_impl.h>
#include <modes/gcm_impl.h>

/*
 * Mechanism info structure passed to KCF during registration.
 */
static const crypto_mech_info_t aes_mech_info_tab[] = {
	/* AES_CCM */
	{SUN_CKM_AES_CCM, AES_CCM_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC},
	/* AES_GCM */
	{SUN_CKM_AES_GCM, AES_GCM_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC},
};

static int aes_common_init_ctx(aes_ctx_t *, crypto_spi_ctx_template_t *,
    crypto_mechanism_t *, crypto_key_t *, int, boolean_t);

static int aes_encrypt_atomic(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);

static int aes_decrypt_atomic(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);

static const crypto_cipher_ops_t aes_cipher_ops = {
	.encrypt_atomic = aes_encrypt_atomic,
	.decrypt_atomic = aes_decrypt_atomic
};

static int aes_create_ctx_template(crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t *, size_t *);
static int aes_free_context(crypto_ctx_t *);

static const crypto_ctx_ops_t aes_ctx_ops = {
	.create_ctx_template = aes_create_ctx_template,
	.free_context = aes_free_context
};

static const crypto_ops_t aes_crypto_ops = {
	&aes_cipher_ops,
	NULL,
	&aes_ctx_ops,
};

static const crypto_provider_info_t aes_prov_info = {
	"AES Software Provider",
	&aes_crypto_ops,
	sizeof (aes_mech_info_tab) / sizeof (crypto_mech_info_t),
	aes_mech_info_tab
};

static crypto_kcf_provider_handle_t aes_prov_handle = 0;

/*
 * Known answers for the end-to-end self-test: AES-256 with 12-byte IVs and
 * 16-byte MACs, as ZFS uses them. Both are from Project Wycheproof
 * (github.com/C2SP/wycheproof, testvectors_v1), as are the vectors used by the
 * crypto tests in tests/zfs-tests/tests/functional/crypto. The GCM message,
 * tcId 115 of aes_gcm_test.json, is long enough to run the bulk loops of the
 * avx code, and its tag covers the ciphertext. CCM has no accelerated mode
 * code, so a short message with AAD, tcId 183 of aes_ccm_test.json, is
 * checked in full.
 */
#define	AES_SELFTEST_MAC_LEN	16

static const uint8_t aes_selftest_gcm_key[32] = {
	0xaf, 0xd5, 0x79, 0xaa, 0x1a, 0xcc, 0xc6, 0x82,
	0xac, 0xa5, 0x4e, 0x14, 0x2a, 0xa6, 0x9d, 0xf0,
	0x98, 0x02, 0xf0, 0x20, 0xb2, 0x4a, 0x42, 0xc4,
	0x1d, 0xb5, 0x8f, 0x69, 0x97, 0xed, 0xc6, 0x78
};

static const uint8_t aes_selftest_gcm_iv[12] = {
	0x9f, 0x79, 0xd1, 0xda, 0x95, 0x74, 0x91, 0x06,
	0x9d, 0x77, 0x44, 0x96
};

static const uint8_t aes_selftest_gcm_pt[513] = {
	0xba, 0xfc, 0x6e, 0x86, 0x5c, 0x48, 0xbd, 0x34,
	0xb7, 0xf9, 0x32, 0x9e, 0x35, 0xcf, 0xb2, 0x86,
	0xcd, 0x4d, 0xc3, 0x1f, 0x83, 0x16, 0x17, 0x12,
	0x18, 0xbf, 0x04, 0x71, 0xdf, 0xfd, 0x35, 0xa3,
	0x30, 0xa1, 0x81, 0x69, 0x7c, 0xa5, 0x17, 0x86,
	0x88, 0xdd, 0x87, 0xef, 0xe5, 0x27, 0x92, 0x4f,
	0x90, 0xd1, 0xc7, 0x8b, 0xa4, 0x0d, 0xe7, 0x09,
	0x52, 0xff, 0x44, 0xc2, 0x6e, 0xfe, 0x21, 0x59,
	0xe5, 0x93, 0x58, 0xf3, 0x93, 0x15, 0x73, 0xdf,
	0x93, 0x73, 0xa7, 0x3b, 0x91, 0xba, 0x95, 0x92,
	0xe1, 0x21, 0x40, 0xcc, 0x00, 0x9f, 0xee, 0xdd,
	0x25, 0x95, 0xe5, 0xb6, 0xf0, 0x66, 0xb5, 0xef,
	0x6d, 0xe9, 0x9d, 0x4c, 0x31, 0x55, 0x2c, 0xec,
	0xb0, 0x61, 0x4f, 0x1d, 0xce, 0x99, 0x0e, 0x46,
	0xe7, 0x69, 0x43, 0x82, 0xf3, 0xcf, 0x3c, 0xcf,
	0xcd, 0x1e, 0xa6, 0x2e, 0x56, 0x3e, 0x5f, 0x0d,
	0xc3, 0x6c, 0xb5, 0xa8, 0x4e, 0x0c, 0x0b, 0x3f,
	0x1f, 0x8f, 0x3f, 0xa9, 0x10, 0x0f, 0x48, 0x71,
	0x95, 0xff, 0x2e, 0x31, 0x69, 0xad, 0x08, 0x13,
	0x6a, 0xa8, 0xad, 0x56, 0x65, 0x48, 0xc9, 0x83,
	0x6a, 0xa0, 0x0d, 0xba, 0xc7, 0x47, 0x16, 0xc2,
	0x6e, 0x83, 0x8c, 0x14, 0x86, 0xa0, 0x08, 0x4d,
	0x3d, 0xfd, 0x69, 0x25, 0x85, 0xe2, 0xe5, 0xae,
	0x7c, 0x75, 0xca, 0xf0, 0xe7, 0xaf, 0x60, 0x21,
	0x9f, 0x96, 0x11, 0x6a, 0xe9, 0x63, 0xb4, 0xa5,
	0x89, 0x9c, 0xb3, 0x0a, 0x12, 0x0d, 0xaa, 0xca,
	0x78, 0x33, 0x77, 0x66, 0x92, 0xc2, 0x5a, 0xd7,
	0xc1, 0x85, 0xe6, 0xa2, 0xd7, 0x0c, 0xe0, 0x3f,
	0xf1, 0x56, 0xcd, 0x25, 0xd7, 0x61, 0x53, 0x53,
	0x9d, 0x68, 0x55, 0x77, 0x3e, 0x21, 0x14, 0x2f,
	0x9b, 0xa0, 0x31, 0x35, 0x62, 0x87, 0x5f, 0x10,
	0x5a, 0x2b, 0x77, 0x0a, 0x15, 0xb5, 0x33, 0xfb,
	0xf5, 0x11, 0x0d, 0xaf, 0xb6, 0x93, 0x29, 0x98,
	0x2a, 0xb4, 0x4e, 0xd1, 0xb9, 0xf3, 0x21, 0xd7,
	0xb7, 0x9a, 0xe1, 0x5a, 0x19, 0xd9, 0xf3, 0xbd,
	0x4c, 0x50, 0x4c, 0x24, 0xb2, 0x3b, 0x81, 0x2d,
	0x51, 0x4c, 0x19, 0xae, 0x2a, 0x34, 0x7c, 0xc1,
	0x8c, 0x12, 0xce, 0x91, 0x5a, 0x0b, 0xad, 0x7c,
	0xc8, 0x9a, 0x87, 0x20, 0xd4, 0xba, 0x5e, 0xe0,
	0x96, 0x4f, 0xe0, 0x5e, 0x4c, 0xc5, 0x9a, 0x13,
	0xf9, 0x2c, 0x67, 0x0b, 0x86, 0x55, 0x07, 0x1e,
	0x21, 0x6f, 0x19, 0xad, 0x05, 0xf4, 0xbb, 0xcc,
	0xa6, 0xdc, 0x7f, 0xee, 0xb1, 0x88, 0xd6, 0x26,
	0x9c, 0x58, 0x06, 0x5c, 0x98, 0xfc, 0xbb, 0xac,
	0x18, 0x3a, 0x9a, 0xbb, 0x38, 0x11, 0xd8, 0x0c,
	0xb4, 0x76, 0x54, 0x4b, 0xd7, 0x4b, 0x26, 0x99,
	0x1f, 0x3d, 0xf9, 0x87, 0xf0, 0xed, 0x0e, 0xa6,
	0x23, 0x86, 0x59, 0xac, 0x09, 0xa2, 0x25, 0x0f,
	0xec, 0xc0, 0x72, 0x3f, 0xfc, 0x51, 0x64, 0x7b,
	0x74, 0xbd, 0xf4, 0x54, 0xf2, 0x6e, 0x11, 0x11,
	0x2c, 0x8b, 0xbd, 0x79, 0x7f, 0x09, 0xa3, 0xbe,
	0x82, 0x51, 0xc6, 0xb5, 0xb3, 0x19, 0xed, 0x95,
	0x37, 0x27, 0x8c, 0xc1, 0xab, 0xed, 0xb3, 0x2a,
	0xa1, 0x08, 0x40, 0x98, 0x4b, 0x96, 0xe8, 0x63,
	0x6b, 0x28, 0x93, 0x35, 0x84, 0x6a, 0xe4, 0xfb,
	0xd4, 0xa0, 0x0f, 0x66, 0x00, 0xd9, 0x8e, 0xbe,
	0x25, 0x88, 0x5c, 0x68, 0xd7, 0x04, 0x3c, 0xe0,
	0xdc, 0x52, 0x29, 0xd7, 0xe9, 0xbd, 0x51, 0xbe,
	0xa9, 0xb8, 0xfe, 0x05, 0x52, 0xf4, 0x06, 0x88,
	0x42, 0x9c, 0x48, 0x26, 0x29, 0xce, 0xd6, 0x23,
	0xf6, 0x07, 0x48, 0x58, 0x14, 0x7e, 0x73, 0xda,
	0x3f, 0xf4, 0xad, 0x2a, 0xe4, 0x5c, 0x1a, 0x1c,
	0x8a, 0x6c, 0x5b, 0x3b, 0x2c, 0x3d, 0x56, 0x8a,
	0x75, 0x66, 0x08, 0x17, 0x9f, 0x63, 0xb5, 0x80,
	0xfd
};

static const uint8_t aes_selftest_gcm_tag[AES_SELFTEST_MAC_LEN] = {
	0x32, 0xb2, 0x76, 0xfd, 0x0c, 0x1d, 0xa7, 0xa8,
	0x23, 0xa5, 0xaf, 0x07, 0x4a, 0xec, 0xac, 0xb5
};

static const uint8_t aes_selftest_ccm_key[32] = {
	0xcb, 0x55, 0x75, 0xf5, 0xc7, 0xc4, 0x5c, 0x91,
	0xcf, 0x32, 0x0b, 0x13, 0x9f, 0xb5, 0x94, 0x23,
	0x75, 0x60, 0xd0, 0xa3, 0xe6, 0xf8, 0x65, 0xa6,
	0x7d, 0x4f, 0x63, 0x3f, 0x2c, 0x08, 0xf0, 0x16
};

static const uint8_t aes_selftest_ccm_iv[12] = {
	0x1a, 0x65, 0x18, 0xf0, 0x2e, 0xde, 0x1d, 0xa6,
	0x80, 0x92, 0x66, 0xd9
};

static const uint8_t aes_selftest_ccm_aad[16] = {
	0x89, 0xcc, 0xe9, 0xfb, 0x47, 0x44, 0x1d, 0x07,
	0xe0, 0x24, 0x5a, 0x66, 0xfe, 0x8b, 0x77, 0x8b
};

static const uint8_t aes_selftest_ccm_pt[16] = {
	0x62, 0x3b, 0x78, 0x50, 0xc3, 0x21, 0xe2, 0xcf,
	0x0c, 0x6f, 0xbc, 0xc8, 0xdf, 0xd1, 0xaf, 0xf2
};

/* The ciphertext followed by the MAC. */
static const uint8_t aes_selftest_ccm_ct[16 + AES_SELFTEST_MAC_LEN] = {
	0x72, 0x2a, 0xc6, 0xa2, 0x26, 0xf4, 0x9c, 0x90,
	0xab, 0x22, 0x52, 0x7a, 0x51, 0x38, 0xb4, 0x01,
	0x2e, 0x4e, 0xe9, 0x97, 0xc7, 0x52, 0x78, 0x3e,
	0x74, 0x3b, 0x36, 0x6b, 0xb6, 0xb3, 0x50, 0xa5
};

typedef struct aes_selftest_vector {
	crypto_mech_type_t	type;
	const uint8_t		*key;
	const uint8_t		*iv;
	const uint8_t		*aad;
	size_t			aad_len;
	const uint8_t		*pt;
	size_t			pt_len;
	/* the last expected_len bytes of the ciphertext and MAC */
	const uint8_t		*expected;
	size_t			expected_len;
} aes_selftest_vector_t;

static const aes_selftest_vector_t aes_selftest_ccm = {
	.type = AES_CCM_MECH_INFO_TYPE,
	.key = aes_selftest_ccm_key,
	.iv = aes_selftest_ccm_iv,
	.aad = aes_selftest_ccm_aad,
	.aad_len = sizeof (aes_selftest_ccm_aad),
	.pt = aes_selftest_ccm_pt,
	.pt_len = sizeof (aes_selftest_ccm_pt),
	.expected = aes_selftest_ccm_ct,
	.expected_len = sizeof (aes_selftest_ccm_ct)
};

static const aes_selftest_vector_t aes_selftest_gcm = {
	.type = AES_GCM_MECH_INFO_TYPE,
	.key = aes_selftest_gcm_key,
	.iv = aes_selftest_gcm_iv,
	.aad = NULL,
	.aad_len = 0,
	.pt = aes_selftest_gcm_pt,
	.pt_len = sizeof (aes_selftest_gcm_pt),
	.expected = aes_selftest_gcm_tag,
	.expected_len = sizeof (aes_selftest_gcm_tag)
};

static void
aes_selftest_data(crypto_data_t *cd, void *buf, size_t len)
{
	memset(cd, 0, sizeof (*cd));
	cd->cd_format = CRYPTO_DATA_RAW;
	cd->cd_length = len;
	cd->cd_raw.iov_base = buf;
	cd->cd_raw.iov_len = len;
}

#define	AES_SELFTEST_MAX_IOV	32

/*
 * Describe len bytes of buf as a uio, as ZFS encryption passes data to the
 * ICP: the data in uneven pieces, which split blocks across iovecs, then
 * the MAC in an iovec of its own.
 */
static void
aes_selftest_uio(crypto_data_t *cd, zfs_uio_t *uio, iovec_t *iov,
    uint8_t *buf, size_t len)
{
	static const size_t splits[] = { 1, 15, 17, 64, 100, 7 };
	size_t datalen = len - AES_SELFTEST_MAC_LEN;
	size_t off = 0;
	int cnt = 0;

	for (int i = 0; off < datalen; i = (i + 1) % ARRAY_SIZE(splits)) {
		size_t n = MIN(splits[i], datalen - off);

		VERIFY3S(cnt, <, AES_SELFTEST_MAX_IOV - 1);
		iov[cnt].iov_base = buf + off;
		iov[cnt].iov_len = n;
		cnt++;
		off += n;
	}
	iov[cnt].iov_base = buf + datalen;
	iov[cnt].iov_len = AES_SELFTEST_MAC_LEN;
	cnt++;

	memset(uio, 0, sizeof (*uio));
	zfs_uio_iov(uio) = iov;
	zfs_uio_iovcnt(uio) = cnt;
	zfs_uio_segflg(uio) = UIO_SYSSPACE;

	memset(cd, 0, sizeof (*cd));
	cd->cd_format = CRYPTO_DATA_UIO;
	cd->cd_length = len;
	cd->cd_uio = uio;
}

/*
 * Encrypt the vector's plaintext, check the end of the ciphertext and MAC,
 * then decrypt it again. Returns B_TRUE if both match. With split, the data
 * goes through as a uio of uneven pieces rather than one buffer.
 */
static boolean_t
aes_selftest_mode(const aes_selftest_vector_t *v, boolean_t split)
{
	size_t ctlen = v->pt_len + AES_SELFTEST_MAC_LEN;
	crypto_key_t ck = { .ck_length = CRYPTO_BYTES2BITS(32),
	    .ck_data = (void *)v->key };
	crypto_mechanism_t mech = { .cm_type = v->type };
	crypto_data_t in, out;
	zfs_uio_t uin, uout;
	union {
		CK_AES_CCM_PARAMS ccm;
		CK_AES_GCM_PARAMS gcm;
	} params;
	boolean_t ok;

	uint8_t *pt = kmem_alloc(ctlen, KM_SLEEP);
	uint8_t *ct = kmem_alloc(ctlen, KM_SLEEP);
	iovec_t *iov = kmem_alloc(2 * AES_SELFTEST_MAX_IOV * sizeof (iovec_t),
	    KM_SLEEP);
	memcpy(pt, v->pt, v->pt_len);

	memset(&params, 0, sizeof (params));
	if (v->type == AES_CCM_MECH_INFO_TYPE) {
		params.ccm.nonce = (uchar_t *)v->iv;
		params.ccm.ulNonceSize = 12;
		params.ccm.authData = (uchar_t *)v->aad;
		params.ccm.ulAuthDataSize = v->aad_len;
		params.ccm.ulMACSize = AES_SELFTEST_MAC_LEN;
		params.ccm.ulDataSize = v->pt_len;
		mech.cm_param_len = sizeof (params.ccm);
	} else {
		params.gcm.pIv = (uchar_t *)v->iv;
		params.gcm.ulIvLen = 12;
		params.gcm.ulIvBits = CRYPTO_BYTES2BITS(12);
		params.gcm.pAAD = (uchar_t *)v->aad;
		params.gcm.ulAADLen = v->aad_len;
		params.gcm.ulTagBits = CRYPTO_BYTES2BITS(AES_SELFTEST_MAC_LEN);
		mech.cm_param_len = sizeof (params.gcm);
	}
	mech.cm_param = (caddr_t)&params;

	if (split) {
		aes_selftest_uio(&in, &uin, iov, pt, ctlen);
		in.cd_length = v->pt_len;
		aes_selftest_uio(&out, &uout, iov + AES_SELFTEST_MAX_IOV, ct,
		    ctlen);
	} else {
		aes_selftest_data(&in, pt, v->pt_len);
		aes_selftest_data(&out, ct, ctlen);
	}
	ok = aes_encrypt_atomic(&mech, &ck, &in, &out, NULL) ==
	    CRYPTO_SUCCESS && out.cd_length == ctlen &&
	    memcmp(ct + ctlen - v->expected_len, v->expected,
	    v->expected_len) == 0;

	/* As in ZFS, both buffers cover the MAC when decrypting. */
	if (v->type == AES_CCM_MECH_INFO_TYPE)
		params.ccm.ulDataSize = ctlen;
	memset(pt, 0, ctlen);
	if (split) {
		aes_selftest_uio(&in, &uin, iov, ct, ctlen);
		aes_selftest_uio(&out, &uout, iov + AES_SELFTEST_MAX_IOV, pt,
		    ctlen);
	} else {
		aes_selftest_data(&in, ct, ctlen);
		aes_selftest_data(&out, pt, ctlen);
	}
	if (aes_decrypt_atomic(&mech, &ck, &in, &out, NULL) !=
	    CRYPTO_SUCCESS || out.cd_length != v->pt_len ||
	    memcmp(pt, v->pt, v->pt_len) != 0)
		ok = B_FALSE;

	kmem_free(iov, 2 * AES_SELFTEST_MAX_IOV * sizeof (iovec_t));
	kmem_free(ct, ctlen);
	kmem_free(pt, ctlen);
	return (ok);
}

/*
 * Check AES-CCM and AES-GCM end to end, with the implementations that were
 * chosen. The AES and GHASH implementations have already been checked on
 * their own, so a GCM failure is put down to the avx code, which has its own
 * AES and GHASH; it is disabled, and the test repeated with what remains.
 */
static void
aes_mode_selftest(void)
{
	const char *name;

	if (!aes_selftest_mode(&aes_selftest_ccm, B_FALSE) ||
	    !aes_selftest_mode(&aes_selftest_ccm, B_TRUE))
		cmn_err(CE_WARN, "ICP: aes-ccm failed its self-test");

	while (!aes_selftest_mode(&aes_selftest_gcm, B_FALSE) ||
	    !aes_selftest_mode(&aes_selftest_gcm, B_TRUE)) {
		if ((name = gcm_impl_disable_avx()) == NULL) {
			cmn_err(CE_WARN, "ICP: aes-gcm failed its self-test");
			break;
		}
		cmn_err(CE_WARN, "ICP: the %s implementation of gcm failed "
		    "its self-test, and has been disabled", name);
	}
}

int
aes_mod_init(void)
{
	/* Determine the fastest available implementation. */
	aes_impl_init();
	gcm_impl_init();
	aes_mode_selftest();

	/* Register with KCF.  If the registration fails, remove the module. */
	if (crypto_register_provider(&aes_prov_info, &aes_prov_handle))
		return (EACCES);

	return (0);
}

int
aes_mod_fini(void)
{
	/* Unregister from KCF if module is registered */
	if (aes_prov_handle != 0) {
		if (crypto_unregister_provider(aes_prov_handle))
			return (EBUSY);

		aes_prov_handle = 0;
	}

	return (0);
}

static int
aes_check_mech_param(crypto_mechanism_t *mechanism, aes_ctx_t **ctx)
{
	void *p = NULL;
	boolean_t param_required = B_TRUE;
	size_t param_len;
	void *(*alloc_fun)(int);
	int rv = CRYPTO_SUCCESS;

	switch (mechanism->cm_type) {
	case AES_CCM_MECH_INFO_TYPE:
		param_len = sizeof (CK_AES_CCM_PARAMS);
		alloc_fun = ccm_alloc_ctx;
		break;
	case AES_GCM_MECH_INFO_TYPE:
		param_len = sizeof (CK_AES_GCM_PARAMS);
		alloc_fun = gcm_alloc_ctx;
		break;
	default:
		__builtin_unreachable();
	}
	if (param_required && mechanism->cm_param != NULL &&
	    mechanism->cm_param_len != param_len) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}
	if (ctx != NULL) {
		p = (alloc_fun)(KM_SLEEP);
		*ctx = p;
	}
	return (rv);
}

/*
 * Initialize key schedules for AES
 */
static int
init_keysched(crypto_key_t *key, void *newbie)
{
	if (key->ck_length < AES_MINBITS ||
	    key->ck_length > AES_MAXBITS) {
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	/* key length must be either 128, 192, or 256 */
	if ((key->ck_length & 63) != 0)
		return (CRYPTO_KEY_SIZE_RANGE);

	aes_init_keysched(key->ck_data, key->ck_length, newbie);
	return (CRYPTO_SUCCESS);
}

/*
 * KCF software provider encrypt entry points.
 */
static int
aes_encrypt_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t template)
{
	aes_ctx_t aes_ctx;
	off_t saved_offset;
	size_t saved_length;
	size_t length_needed;
	int ret;

	memset(&aes_ctx, 0, sizeof (aes_ctx_t));

	ASSERT(ciphertext != NULL);

	if ((ret = aes_check_mech_param(mechanism, NULL)) != CRYPTO_SUCCESS)
		return (ret);

	ret = aes_common_init_ctx(&aes_ctx, template, mechanism, key,
	    KM_SLEEP, B_TRUE);
	if (ret != CRYPTO_SUCCESS)
		return (ret);

	switch (mechanism->cm_type) {
	case AES_CCM_MECH_INFO_TYPE:
		length_needed = plaintext->cd_length + aes_ctx.ac_mac_len;
		break;
	case AES_GCM_MECH_INFO_TYPE:
		length_needed = plaintext->cd_length + aes_ctx.ac_tag_len;
		break;
	default:
		__builtin_unreachable();
	}

	/* return size of buffer needed to store output */
	if (ciphertext->cd_length < length_needed) {
		ciphertext->cd_length = length_needed;
		ret = CRYPTO_BUFFER_TOO_SMALL;
		goto out;
	}

	saved_offset = ciphertext->cd_offset;
	saved_length = ciphertext->cd_length;

	/*
	 * Do an update on the specified input data.
	 */
	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(&aes_ctx, plaintext, ciphertext,
		    aes_encrypt_contiguous_blocks);
		break;
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(&aes_ctx, plaintext, ciphertext,
		    aes_encrypt_contiguous_blocks);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		if (mechanism->cm_type == AES_CCM_MECH_INFO_TYPE) {
			ret = ccm_encrypt_final((ccm_ctx_t *)&aes_ctx,
			    ciphertext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_xor_block);
			if (ret != CRYPTO_SUCCESS)
				goto out;
			ASSERT0(aes_ctx.ac_remainder_len);
		} else if (mechanism->cm_type == AES_GCM_MECH_INFO_TYPE) {
			ret = gcm_encrypt_final((gcm_ctx_t *)&aes_ctx,
			    ciphertext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
			if (ret != CRYPTO_SUCCESS)
				goto out;
			ASSERT0(aes_ctx.ac_remainder_len);
		} else {
			ASSERT0(aes_ctx.ac_remainder_len);
		}

		if (plaintext != ciphertext) {
			ciphertext->cd_length =
			    ciphertext->cd_offset - saved_offset;
		}
	} else {
		ciphertext->cd_length = saved_length;
	}
	ciphertext->cd_offset = saved_offset;

out:
	if (aes_ctx.ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
		memset(aes_ctx.ac_keysched, 0, aes_ctx.ac_keysched_len);
		kmem_free(aes_ctx.ac_keysched, aes_ctx.ac_keysched_len);
	}
	if (aes_ctx.ac_flags & GCM_MODE) {
		gcm_clear_ctx((gcm_ctx_t *)&aes_ctx);
	}
	return (ret);
}

static int
aes_decrypt_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t template)
{
	aes_ctx_t aes_ctx;
	off_t saved_offset;
	size_t saved_length;
	size_t length_needed;
	int ret;

	memset(&aes_ctx, 0, sizeof (aes_ctx_t));

	ASSERT(plaintext != NULL);

	if ((ret = aes_check_mech_param(mechanism, NULL)) != CRYPTO_SUCCESS)
		return (ret);

	ret = aes_common_init_ctx(&aes_ctx, template, mechanism, key,
	    KM_SLEEP, B_FALSE);
	if (ret != CRYPTO_SUCCESS)
		return (ret);

	switch (mechanism->cm_type) {
	case AES_CCM_MECH_INFO_TYPE:
		length_needed = aes_ctx.ac_data_len;
		break;
	case AES_GCM_MECH_INFO_TYPE:
		length_needed = ciphertext->cd_length - aes_ctx.ac_tag_len;
		break;
	default:
		__builtin_unreachable();
	}

	/* return size of buffer needed to store output */
	if (plaintext->cd_length < length_needed) {
		plaintext->cd_length = length_needed;
		ret = CRYPTO_BUFFER_TOO_SMALL;
		goto out;
	}

	saved_offset = plaintext->cd_offset;
	saved_length = plaintext->cd_length;

	/*
	 * Do an update on the specified input data.
	 */
	switch (ciphertext->cd_format) {
	case CRYPTO_DATA_RAW:
		ret = crypto_update_iov(&aes_ctx, ciphertext, plaintext,
		    aes_decrypt_contiguous_blocks);
		break;
	case CRYPTO_DATA_UIO:
		ret = crypto_update_uio(&aes_ctx, ciphertext, plaintext,
		    aes_decrypt_contiguous_blocks);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		if (mechanism->cm_type == AES_CCM_MECH_INFO_TYPE) {
			ASSERT(aes_ctx.ac_processed_data_len
			    == aes_ctx.ac_data_len);
			ASSERT(aes_ctx.ac_processed_mac_len
			    == aes_ctx.ac_mac_len);
			ret = ccm_decrypt_final((ccm_ctx_t *)&aes_ctx,
			    plaintext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
			ASSERT0(aes_ctx.ac_remainder_len);
			if ((ret == CRYPTO_SUCCESS) &&
			    (ciphertext != plaintext)) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			} else {
				plaintext->cd_length = saved_length;
			}
		} else if (mechanism->cm_type == AES_GCM_MECH_INFO_TYPE) {
			ret = gcm_decrypt_final((gcm_ctx_t *)&aes_ctx,
			    plaintext, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_xor_block);
			ASSERT0(aes_ctx.ac_remainder_len);
			if ((ret == CRYPTO_SUCCESS) &&
			    (ciphertext != plaintext)) {
				plaintext->cd_length =
				    plaintext->cd_offset - saved_offset;
			} else {
				plaintext->cd_length = saved_length;
			}
		} else
			__builtin_unreachable();
	} else {
		plaintext->cd_length = saved_length;
	}
	plaintext->cd_offset = saved_offset;

out:
	if (aes_ctx.ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
		memset(aes_ctx.ac_keysched, 0, aes_ctx.ac_keysched_len);
		kmem_free(aes_ctx.ac_keysched, aes_ctx.ac_keysched_len);
	}

	if (aes_ctx.ac_flags & CCM_MODE) {
		if (aes_ctx.ac_pt_buf != NULL) {
			vmem_free(aes_ctx.ac_pt_buf, aes_ctx.ac_data_len);
		}
	} else if (aes_ctx.ac_flags & GCM_MODE) {
		gcm_clear_ctx((gcm_ctx_t *)&aes_ctx);
	}

	return (ret);
}

/*
 * KCF software provider context template entry points.
 */
static int
aes_create_ctx_template(crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *tmpl, size_t *tmpl_size)
{
	void *keysched;
	size_t size;
	int rv;

	if (mechanism->cm_type != AES_CCM_MECH_INFO_TYPE &&
	    mechanism->cm_type != AES_GCM_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	if ((keysched = aes_alloc_keysched(&size, KM_SLEEP)) == NULL) {
		return (CRYPTO_HOST_MEMORY);
	}

	/*
	 * Initialize key schedule.  Key length information is stored
	 * in the key.
	 */
	if ((rv = init_keysched(key, keysched)) != CRYPTO_SUCCESS) {
		memset(keysched, 0, size);
		kmem_free(keysched, size);
		return (rv);
	}

	*tmpl = keysched;
	*tmpl_size = size;

	return (CRYPTO_SUCCESS);
}


static int
aes_free_context(crypto_ctx_t *ctx)
{
	aes_ctx_t *aes_ctx = ctx->cc_provider_private;

	if (aes_ctx != NULL) {
		if (aes_ctx->ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
			ASSERT(aes_ctx->ac_keysched_len != 0);
			memset(aes_ctx->ac_keysched, 0,
			    aes_ctx->ac_keysched_len);
			kmem_free(aes_ctx->ac_keysched,
			    aes_ctx->ac_keysched_len);
		}
		crypto_free_mode_ctx(aes_ctx);
		ctx->cc_provider_private = NULL;
	}

	return (CRYPTO_SUCCESS);
}


static int
aes_common_init_ctx(aes_ctx_t *aes_ctx, crypto_spi_ctx_template_t *template,
    crypto_mechanism_t *mechanism, crypto_key_t *key, int kmflag,
    boolean_t is_encrypt_init)
{
	int rv = CRYPTO_SUCCESS;
	void *keysched;
	size_t size = 0;

	if (template == NULL) {
		if ((keysched = aes_alloc_keysched(&size, kmflag)) == NULL)
			return (CRYPTO_HOST_MEMORY);
		/*
		 * Initialize key schedule.
		 * Key length is stored in the key.
		 */
		if ((rv = init_keysched(key, keysched)) != CRYPTO_SUCCESS) {
			kmem_free(keysched, size);
			return (rv);
		}

		aes_ctx->ac_flags |= PROVIDER_OWNS_KEY_SCHEDULE;
		aes_ctx->ac_keysched_len = size;
	} else {
		keysched = template;
	}
	aes_ctx->ac_keysched = keysched;

	switch (mechanism->cm_type) {
	case AES_CCM_MECH_INFO_TYPE:
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_CCM_PARAMS)) {
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		rv = ccm_init_ctx((ccm_ctx_t *)aes_ctx, mechanism->cm_param,
		    kmflag, is_encrypt_init, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_xor_block);
		break;
	case AES_GCM_MECH_INFO_TYPE:
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (CK_AES_GCM_PARAMS)) {
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		rv = gcm_init_ctx((gcm_ctx_t *)aes_ctx, mechanism->cm_param,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;
	}

	if (rv != CRYPTO_SUCCESS) {
		if (aes_ctx->ac_flags & PROVIDER_OWNS_KEY_SCHEDULE) {
			memset(keysched, 0, size);
			kmem_free(keysched, size);
		}
	}

	return (rv);
}
