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

#include <sys/crypto/api.h>
#include <sys/crypto/icp.h>
#include <sys/sha2.h>

#include "unit.h"

/* RFC 4231, test case 6: the 131-byte key must be hashed first. */
static char hmac_message[] =
	"Test Using Larger Than Block-Size Key - Hash Key First";
static const uint8_t hmac_digest[64] = {
	0x80, 0xb2, 0x42, 0x63, 0xc7, 0xc1, 0xa3, 0xeb,
	0xb7, 0x14, 0x93, 0xc1, 0xdd, 0x7b, 0xe8, 0xb4,
	0x9b, 0x46, 0xd1, 0xf4, 0x1b, 0x4a, 0xee, 0xc1,
	0x12, 0x1b, 0x01, 0x37, 0x83, 0xf8, 0xf3, 0x52,
	0x6b, 0x56, 0xd0, 0x37, 0xe0, 0x5f, 0x25, 0x98,
	0xbd, 0x0f, 0xd2, 0x21, 0x5d, 0x6a, 0x1e, 0x52,
	0x95, 0xe6, 0x4f, 0x73, 0xf6, 0x3f, 0x0a, 0xec,
	0x8b, 0x91, 0x5a, 0x98, 0x5d, 0x78, 0x65, 0x98,
};

static MunitResult
test_hmac(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t multipart =
	    strcmp(munit_parameters_get(params, "mode"), "multipart") == 0;
	boolean_t templated =
	    strcmp(munit_parameters_get(params, "template"), "yes") == 0;
	boolean_t uio =
	    strcmp(munit_parameters_get(params, "format"), "uio") == 0;
	uint8_t keybytes[131];
	uint8_t digest[69];
	crypto_key_t key = {
		.ck_data = keybytes,
		.ck_length = sizeof (keybytes) * 8,
	};
	crypto_mechanism_t mech = {
		.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC),
	};
	crypto_ctx_template_t template = NULL;
	crypto_data_t input = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_length = sizeof (hmac_message) - 1,
		.cd_raw = { hmac_message, sizeof (hmac_message) - 1 },
	};
	crypto_data_t output = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 5,
		.cd_length = 64,
		.cd_raw = { digest, sizeof (digest) },
	};
	struct iovec input_iov[] = {
		{ hmac_message, 17 },
		{ hmac_message + 17, sizeof (hmac_message) - 18 },
	};
	struct iovec output_iov[] = {
		{ digest, 23 },
		{ digest + 23, sizeof (digest) - 23 },
	};
	zfs_uio_t input_uio = {
		.uio_iov = input_iov,
		.uio_iovcnt = ARRAY_SIZE(input_iov),
		.uio_segflg = UIO_SYSSPACE,
	};
	zfs_uio_t output_uio = {
		.uio_iov = output_iov,
		.uio_iovcnt = ARRAY_SIZE(output_iov),
		.uio_segflg = UIO_SYSSPACE,
	};

	memset(keybytes, 0xaa, sizeof (keybytes));
	memset(digest, 0xa5, sizeof (digest));
	if (uio) {
		input.cd_format = CRYPTO_DATA_UIO;
		input.cd_uio = &input_uio;
		output.cd_format = CRYPTO_DATA_UIO;
		output.cd_uio = &output_uio;
	}
	if (templated)
		unit_ok(crypto_create_ctx_template(&mech, &key, &template));

	if (multipart) {
		crypto_context_t ctx;

		unit_ok(crypto_mac_init(&mech, &key, template, &ctx));
		input.cd_length = 19;
		unit_ok(crypto_mac_update(ctx, &input));
		input.cd_offset = 19;
		input.cd_length = sizeof (hmac_message) - 20;
		unit_ok(crypto_mac_update(ctx, &input));

		const size_t short_lengths[] = { 0, 8, 63 };
		for (size_t i = 0; i < ARRAY_SIZE(short_lengths); i++) {
			output.cd_length = short_lengths[i];
			unit_err(crypto_mac_final(ctx, &output),
			    CRYPTO_BUFFER_TOO_SMALL);
			unit_eq(output.cd_length, 64);
		}
		unit_ok(crypto_mac_final(ctx, &output));
	} else {
		unit_ok(crypto_mac(&mech, &input, &key, template, &output));
	}

	crypto_destroy_ctx_template(template);
	unit_eq(output.cd_length, 64);
	munit_assert_memory_equal(64, digest + 5, hmac_digest);
	for (size_t i = 0; i < 5; i++)
		unit_eq(digest[i], 0xa5);
	return (MUNIT_OK);
}

static MunitResult
test_mechanisms(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	unit_eq(crypto_mech2id(SUN_CKM_AES_CCM), UINT64_C(0x100000000));
	unit_eq(crypto_mech2id(SUN_CKM_AES_GCM), UINT64_C(0x100000001));
	unit_eq(crypto_mech2id(SUN_CKM_SHA512_HMAC), UINT64_C(0x200000000));
	unit_eq(crypto_mech2id("unknown"), CRYPTO_MECH_INVALID);
	unit_err(crypto_mac_update(NULL, NULL), CRYPTO_INVALID_CONTEXT);
	unit_err(crypto_mac_final(NULL, NULL), CRYPTO_INVALID_CONTEXT);
	crypto_ctx_template_t template = NULL;
	unit_err(crypto_create_ctx_template(NULL, NULL, &template),
	    CRYPTO_MECHANISM_INVALID);
	unit_err(crypto_create_ctx_template(NULL, NULL, NULL),
	    CRYPTO_ARGUMENTS_BAD);
	crypto_destroy_ctx_template(NULL);
	return (MUNIT_OK);
}

static MunitResult
test_mechanism_errors(const MunitParameter params[], void *data)
{
	(void) data;
	crypto_mechanism_t mech = {
		.cm_type = strtoull(munit_parameters_get(params, "id"),
		    NULL, 0),
	};
	boolean_t cipher = mech.cm_type == UINT64_C(0x100000000) ||
	    mech.cm_type == UINT64_C(0x100000001);
	boolean_t mac = mech.cm_type == UINT64_C(0x200000000);
	boolean_t unsupported = cipher || mac ||
	    mech.cm_type == UINT64_C(0x10000001f) ||
	    mech.cm_type == UINT64_C(0x200000027);
	int error = unsupported ? CRYPTO_MECH_NOT_SUPPORTED :
	    CRYPTO_MECHANISM_INVALID;
	crypto_context_t ctx;
	crypto_ctx_template_t template = NULL;

	if (!cipher) {
		unit_err(crypto_encrypt(&mech, NULL, NULL, NULL, NULL), error);
		unit_err(crypto_decrypt(&mech, NULL, NULL, NULL, NULL), error);
	}
	if (!mac) {
		unit_err(crypto_mac(&mech, NULL, NULL, NULL, NULL), error);
		unit_err(crypto_mac_init(&mech, NULL, NULL, &ctx), error);
	}
	if (!mac && !cipher) {
		unit_err(crypto_create_ctx_template(&mech, NULL, &template),
		    error);
		unit_null(template);
	}
	return (MUNIT_OK);
}

static MunitResult
test_aes_template(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t gcm =
	    strcmp(munit_parameters_get(params, "mode"), "gcm") == 0;
	boolean_t use_uio =
	    strcmp(munit_parameters_get(params, "format"), "uio") == 0;
	/* CCM: aes_ccm_test.json case 13; GCM: McGrew/Viega case 2. */
	uint8_t keybytes[16] = { 0 };
	uint8_t iv[12] = { 0 };
	uint8_t aad[8];
	uint8_t message[17] = { 0 };
	uint8_t expected[33];
	size_t length = gcm ? 16 : 17;
	if (gcm) {
		memcpy(expected,
		    "\x03\x88\xda\xce\x60\xb6\xa3\x92\xf3\x28\xc2\xb9"
		    "\x71\xb2\xfe\x78\xab\x6e\x47\xd4\x2c\xec\x13\xbd"
		    "\xf5\x3a\x67\xb2\x12\x57\xbd\xdf", 32);
	} else {
		memcpy(keybytes,
		    "\xd9\x7c\x9b\x04\x3b\xdc\xcf\xd5"
		    "\x94\x91\xa9\x95\xe7\x8f\x16\x96", 16);
		memcpy(iv, "\xef\x42\x32\x40\x35\x88\x30\xdf\x91\x55\x06\xa3",
		    12);
		memcpy(aad, "\x3d\xdb\xa7\xb3\xab\x69\xc8\xb2", 8);
		memcpy(message, "\xf0\x47\x59\x4a\x5c\xff\xda\x64\x30\x3a"
		    "\x80\xb2\xfa\x6a\x95\x71\x69", 17);
		memcpy(expected,
		    "\xe0\xca\xf2\xa9\xd5\x0f\x70\xec\xaa\x43\xb4\xa2"
		    "\x87\xc3\xb3\x4a\x99\xcf\xf4\xc6\x18\x82\xb4\x13"
		    "\xb6\x86\xff\x35\xb6\x3a\x3a\x73\xde", 33);
	}
	crypto_key_t key = { .ck_data = keybytes, .ck_length = 128 };
	CK_AES_CCM_PARAMS ccm = {
		.ulMACSize = 16, .ulNonceSize = sizeof (iv),
		.ulAuthDataSize = sizeof (aad), .nonce = iv, .authData = aad,
	};
	CK_AES_GCM_PARAMS gcm_params = {
		.pIv = iv, .ulIvLen = sizeof (iv), .ulIvBits = 96,
		.ulTagBits = 128,
	};
	crypto_mechanism_t mech = {
		.cm_type = crypto_mech2id(gcm ?
		    SUN_CKM_AES_GCM : SUN_CKM_AES_CCM),
		.cm_param = (void *)(gcm ? (void *)&gcm_params : (void *)&ccm),
		.cm_param_len = gcm ? sizeof (gcm_params) : sizeof (ccm),
	};
	crypto_mech_type_t type = mech.cm_type;
	crypto_ctx_template_t template = NULL;
	unit_ok(crypto_create_ctx_template(&mech, &key, &template));

	for (int pass = 0; pass < 2; pass++) {
		uint8_t input[48], encrypted[48], decrypted[48];
		struct iovec ii[] = { { input, 16 }, { input + 16, 32 } };
		struct iovec ei[] = {
			{ encrypted, 19 }, { encrypted + 19, 29 } };
		struct iovec di[] = {
			{ decrypted, 17 }, { decrypted + 17, 31 } };
		zfs_uio_t iu = { .uio_iov = ii, .uio_iovcnt = 2,
			.uio_segflg = UIO_SYSSPACE };
		zfs_uio_t eu = { .uio_iov = ei, .uio_iovcnt = 2,
			.uio_segflg = UIO_SYSSPACE };
		zfs_uio_t du = { .uio_iov = di, .uio_iovcnt = 2,
			.uio_segflg = UIO_SYSSPACE };
		crypto_data_t in = {
			.cd_format = CRYPTO_DATA_RAW, .cd_offset = 5,
			.cd_length = length,
			.cd_raw = { input, sizeof (input) },
		};
		crypto_data_t out = {
			.cd_format = CRYPTO_DATA_RAW, .cd_offset = 5,
			.cd_length = length + 16,
			.cd_raw = { encrypted, sizeof (encrypted) },
		};
		crypto_data_t plain = {
			.cd_format = CRYPTO_DATA_RAW, .cd_offset = 5,
			.cd_length = length,
			.cd_raw = { decrypted, sizeof (decrypted) },
		};
		memset(input, 0xa5, sizeof (input));
		memcpy(input + 5, message, length);
		memset(encrypted, 0xa5, sizeof (encrypted));
		memset(decrypted, 0xa5, sizeof (decrypted));
		if (use_uio) {
			in.cd_format = out.cd_format = plain.cd_format =
			    CRYPTO_DATA_UIO;
			in.cd_uio = &iu;
			out.cd_uio = &eu;
			plain.cd_uio = &du;
		}
		ccm.ulDataSize = length;
		unit_ok(crypto_encrypt(&mech, &in, &key, template, &out));
		unit_eq(mech.cm_type, type);
		unit_eq(out.cd_offset, 5);
		unit_eq(out.cd_length, length + 16);
		munit_assert_memory_equal(length + 16, encrypted + 5, expected);
		unit_eq(encrypted[4], 0xa5);
		unit_eq(encrypted[5 + length + 16], 0xa5);
		ccm.ulDataSize = length + 16;
		unit_ok(crypto_decrypt(&mech, &out, &key, template, &plain));
		unit_eq(plain.cd_offset, 5);
		unit_eq(plain.cd_length, length);
		munit_assert_memory_equal(length, decrypted + 5, message);
		unit_eq(decrypted[4], 0xa5);
		unit_eq(decrypted[5 + length], 0xa5);
		encrypted[5 + length + 15] ^= 1;
		unit_err(crypto_decrypt(&mech, &out, &key, template, &plain),
		    CRYPTO_INVALID_MAC);
	}
	crypto_destroy_ctx_template(template);
	return (MUNIT_OK);
}

static MunitResult
test_hmac_final_short_uio(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	uint8_t keybyte = 0x71;
	uint8_t digest[32];
	crypto_key_t key = { .ck_data = &keybyte, .ck_length = 8 };
	crypto_mechanism_t mech = {
		.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC),
	};
	struct iovec iov = { digest, sizeof (digest) };
	zfs_uio_t uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_segflg = UIO_SYSSPACE,
	};
	crypto_data_t output = {
		.cd_format = CRYPTO_DATA_UIO,
		.cd_length = 64,
		.cd_uio = &uio,
	};
	crypto_context_t ctx;

	unit_ok(crypto_mac_init(&mech, &key, NULL, &ctx));
	unit_err(crypto_mac_final(ctx, &output), CRYPTO_DATA_LEN_RANGE);
	unit_zero(output.cd_length);
	return (MUNIT_OK);
}

static const MunitParameterEnum hmac_params[] = {
	UNIT_PARAM("mode", "atomic", "multipart"),
	UNIT_PARAM("template", "no", "yes"),
	UNIT_PARAM("format", "raw", "uio"),
	{ 0 },
};

static const MunitParameterEnum mechanism_params[] = {
	UNIT_PARAM("id", "0", "0xffffffffffffffff",
	    "0x100000000", "0x100000001", "0x10000001f", "0x100000020",
	    "0x200000000", "0x200000027", "0x200000028"),
	{ 0 },
};

static const MunitParameterEnum aes_params[] = {
	UNIT_PARAM("mode", "ccm", "gcm"),
	UNIT_PARAM("format", "raw", "uio"),
	{ 0 },
};

static const MunitTest icp_tests[] = {
	UNIT_TEST("mechanisms", test_mechanisms),
	UNIT_TEST("mechanism_errors", test_mechanism_errors, mechanism_params),
	UNIT_TEST("aes_template", test_aes_template, aes_params),
	UNIT_TEST("hmac", test_hmac, hmac_params),
	UNIT_TEST("hmac_final_short_uio", test_hmac_final_short_uio),
	{ 0 },
};

static const MunitSuite icp_test_suite = {
	"icp.",
	icp_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	int error = icp_init();
	if (error != 0)
		return (1);
	error = munit_suite_main(&icp_test_suite, NULL, argc, argv);
	icp_fini();
	return (error);
}
