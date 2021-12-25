/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 */

#include <sys/crypto/api.h>
#include <sys/sha2.h>
#include <sys/hkdf.h>

static int
hkdf_sha512_extract(uint8_t *salt, uint_t salt_len, uint8_t *key_material,
    uint_t km_len, uint8_t *out_buf)
{
	int ret;
	crypto_mechanism_t mech;
	crypto_key_t key;
	crypto_data_t input_cd, output_cd;

	/* initialize HMAC mechanism */
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	mech.cm_param = NULL;
	mech.cm_param_len = 0;

	/* initialize the salt as a crypto key */
	key.ck_length = CRYPTO_BYTES2BITS(salt_len);
	key.ck_data = salt;

	/* initialize crypto data for the input and output data */
	input_cd.cd_format = CRYPTO_DATA_RAW;
	input_cd.cd_offset = 0;
	input_cd.cd_length = km_len;
	input_cd.cd_raw.iov_base = (char *)key_material;
	input_cd.cd_raw.iov_len = input_cd.cd_length;

	output_cd.cd_format = CRYPTO_DATA_RAW;
	output_cd.cd_offset = 0;
	output_cd.cd_length = SHA512_DIGEST_LENGTH;
	output_cd.cd_raw.iov_base = (char *)out_buf;
	output_cd.cd_raw.iov_len = output_cd.cd_length;

	ret = crypto_mac(&mech, &input_cd, &key, NULL, &output_cd);
	if (ret != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static int
hkdf_sha512_expand(uint8_t *extract_key, uint8_t *info, uint_t info_len,
    uint8_t *out_buf, uint_t out_len)
{
	int ret;
	crypto_mechanism_t mech;
	crypto_context_t ctx;
	crypto_key_t key;
	crypto_data_t T_cd, info_cd, c_cd;
	uint_t i, T_len = 0, pos = 0;
	uint8_t c;
	uint_t N = (out_len + SHA512_DIGEST_LENGTH) / SHA512_DIGEST_LENGTH;
	uint8_t T[SHA512_DIGEST_LENGTH];

	if (N > 255)
		return (SET_ERROR(EINVAL));

	/* initialize HMAC mechanism */
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	mech.cm_param = NULL;
	mech.cm_param_len = 0;

	/* initialize the salt as a crypto key */
	key.ck_length = CRYPTO_BYTES2BITS(SHA512_DIGEST_LENGTH);
	key.ck_data = extract_key;

	/* initialize crypto data for the input and output data */
	T_cd.cd_format = CRYPTO_DATA_RAW;
	T_cd.cd_offset = 0;
	T_cd.cd_raw.iov_base = (char *)T;

	c_cd.cd_format = CRYPTO_DATA_RAW;
	c_cd.cd_offset = 0;
	c_cd.cd_length = 1;
	c_cd.cd_raw.iov_base = (char *)&c;
	c_cd.cd_raw.iov_len = c_cd.cd_length;

	info_cd.cd_format = CRYPTO_DATA_RAW;
	info_cd.cd_offset = 0;
	info_cd.cd_length = info_len;
	info_cd.cd_raw.iov_base = (char *)info;
	info_cd.cd_raw.iov_len = info_cd.cd_length;

	for (i = 1; i <= N; i++) {
		c = i;

		T_cd.cd_length = T_len;
		T_cd.cd_raw.iov_len = T_cd.cd_length;

		ret = crypto_mac_init(&mech, &key, NULL, &ctx);
		if (ret != CRYPTO_SUCCESS)
			return (SET_ERROR(EIO));

		ret = crypto_mac_update(ctx, &T_cd);
		if (ret != CRYPTO_SUCCESS)
			return (SET_ERROR(EIO));

		ret = crypto_mac_update(ctx, &info_cd);
		if (ret != CRYPTO_SUCCESS)
			return (SET_ERROR(EIO));

		ret = crypto_mac_update(ctx, &c_cd);
		if (ret != CRYPTO_SUCCESS)
			return (SET_ERROR(EIO));

		T_len = SHA512_DIGEST_LENGTH;
		T_cd.cd_length = T_len;
		T_cd.cd_raw.iov_len = T_cd.cd_length;

		ret = crypto_mac_final(ctx, &T_cd);
		if (ret != CRYPTO_SUCCESS)
			return (SET_ERROR(EIO));

		bcopy(T, out_buf + pos,
		    (i != N) ? SHA512_DIGEST_LENGTH : (out_len - pos));
		pos += SHA512_DIGEST_LENGTH;
	}

	return (0);
}

/*
 * HKDF is designed to be a relatively fast function for deriving keys from a
 * master key + a salt. We use this function to generate new encryption keys
 * so as to avoid hitting the cryptographic limits of the underlying
 * encryption modes. Note that, for the sake of deriving encryption keys, the
 * info parameter is called the "salt" everywhere else in the code.
 */
int
hkdf_sha512(uint8_t *key_material, uint_t km_len, uint8_t *salt,
    uint_t salt_len, uint8_t *info, uint_t info_len, uint8_t *output_key,
    uint_t out_len)
{
	int ret;
	uint8_t extract_key[SHA512_DIGEST_LENGTH];

	ret = hkdf_sha512_extract(salt, salt_len, key_material, km_len,
	    extract_key);
	if (ret != 0)
		return (ret);

	ret = hkdf_sha512_expand(extract_key, info, info_len, output_key,
	    out_len);
	if (ret != 0)
		return (ret);

	return (0);
}
