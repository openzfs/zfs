/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This is a userspace test driver for the ICP. It has two modes:
 *
 * "correctness" (-c <testfile>):
 *   Load a file full of test vectors. For each implementation of the named
 *   algorithm, loop over the tests, and run encrypt and decrypt with the
 *   provided parameters and confirm they either do (result=valid) or do not
 *   (result=invalid) succeed.
 *
 * "performance" (-p <alg>)
 *   For each implementation of the named algorithm, run 1000 rounds of
 *   encrypt() on a range of power-2 sizes of input data from 2^10 (1K) to
 *   2^19 (512K).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <sys/crypto/icp.h>
#include <sys/crypto/api.h>

/* for zfs_nicenum, zfs_nicebytes */
#include <libzutil.h>

/* ========== */

/* types and data for both modes */

/* valid test algorithms */
typedef enum {
	ALG_NONE,
	ALG_AES_GCM,
	ALG_AES_CCM,
} crypto_test_alg_t;

/*
 * Generally the ICP expects zero-length data to still require a valid
 * (non-NULL) pointer, even though it will never read from it. This is a
 * convenient valid item for tjat case.
 */
static uint8_t val_empty[1] = {0};

/* Strings for error returns */
static const char *crypto_errstr[] = {
	[CRYPTO_SUCCESS] =		    "CRYPTO_SUCCESS",
	[CRYPTO_HOST_MEMORY] =		    "CRYPTO_HOST_MEMORY",
	[CRYPTO_FAILED] =		    "CRYPTO_FAILED",
	[CRYPTO_ARGUMENTS_BAD] =	    "CRYPTO_ARGUMENTS_BAD",
	[CRYPTO_DATA_LEN_RANGE] =	    "CRYPTO_DATA_LEN_RANGE",
	[CRYPTO_ENCRYPTED_DATA_LEN_RANGE] = "CRYPTO_ENCRYPTED_DATA_LEN_RANGE",
	[CRYPTO_KEY_SIZE_RANGE] =	    "CRYPTO_KEY_SIZE_RANGE",
	[CRYPTO_KEY_TYPE_INCONSISTENT] =    "CRYPTO_KEY_TYPE_INCONSISTENT",
	[CRYPTO_MECHANISM_INVALID] =	    "CRYPTO_MECHANISM_INVALID",
	[CRYPTO_MECHANISM_PARAM_INVALID] =  "CRYPTO_MECHANISM_PARAM_INVALID",
	[CRYPTO_SIGNATURE_INVALID] =	    "CRYPTO_SIGNATURE_INVALID",
	[CRYPTO_BUFFER_TOO_SMALL] =	    "CRYPTO_BUFFER_TOO_SMALL",
	[CRYPTO_NOT_SUPPORTED] =	    "CRYPTO_NOT_SUPPORTED",
	[CRYPTO_INVALID_CONTEXT] =	    "CRYPTO_INVALID_CONTEXT",
	[CRYPTO_INVALID_MAC] =		    "CRYPTO_INVALID_MAC",
	[CRYPTO_MECH_NOT_SUPPORTED] =	    "CRYPTO_MECH_NOT_SUPPORTED",
	[CRYPTO_INVALID_PROVIDER_ID] =	    "CRYPTO_INVALID_PROVIDER_ID",
	[CRYPTO_BUSY] =			    "CRYPTO_BUSY",
	[CRYPTO_UNKNOWN_PROVIDER] =	    "CRYPTO_UNKNOWN_PROVIDER",
};

/* what to output; driven by -v switch */
typedef enum {
	OUT_SUMMARY,
	OUT_FAIL,
	OUT_ALL,
} crypto_test_outmode_t;


/* ========== */

/* types and data for correctness tests */

/* most ICP inputs are separate val & len */
typedef struct {
	uint8_t		*val;
	size_t		len;
} crypto_test_val_t;

/* tests can be expected to pass (valid) or expected to fail (invalid) */
typedef enum {
	RS_NONE = 0,
	RS_VALID,
	RS_INVALID,
} crypto_test_result_t;

/* a single test, loaded from the test file */
typedef struct crypto_test crypto_test_t;
struct crypto_test {
	crypto_test_t		*next;	    /* ptr to next test */
	char			*fileloc;   /* file:line of test in file */
	crypto_test_alg_t	alg;	    /* alg, for convenience */

	/* id, comment and flags are for output */
	uint64_t		id;
	char			*comment;
	char			*flags;

	/*
	 * raw test params. these are hex strings in the test file, which
	 * we convert on load.
	 */
	crypto_test_val_t	iv;
	crypto_test_val_t	key;
	crypto_test_val_t	msg;
	crypto_test_val_t	ct;
	crypto_test_val_t	aad;
	crypto_test_val_t	tag;

	/* expected result */
	crypto_test_result_t	result;
};

/* ========== */

/* test file loader */

/*
 * helper; split a 'key: value\n' line into separate key and value. original
 * line is modified; \0 will be inserted at end of key and end of value.
 */
static boolean_t
split_kv(char *line, char **kp, char **vp)
{
	char *c = strstr(line, ":");
	if (c == NULL)
		return (B_FALSE);


	*c++ = '\0';
	while (*c == ' ')
		c++;

	char *v = c;
	c = strchr(v, '\n');
	if (c != NULL) {
		*c++ = '\0';
		if (*c != '\0')
			return (B_FALSE);
	}

	*kp = line;
	*vp = v;
	return (B_TRUE);
}

/*
 * helper; parse decimal number to uint64
 */
static boolean_t
parse_num(char *v, uint64_t *np)
{
	char *c = NULL;
	errno = 0;
	uint64_t n = strtoull(v, &c, 10);
	if (*v == '\0' || *c != '\0' || errno != 0 ||
	    n >= UINT32_MAX || n == 0)
		return (B_FALSE);
	*np = n;
	return (B_TRUE);
}

/*
 * load tests from the test file. returns a linked list of tests, and the
 * test algorithm in *algp.
 */
static crypto_test_t *
load_tests(const char *filepath, crypto_test_alg_t *algp)
{
	crypto_test_t *tests = NULL, *tail = NULL;
	char *buf = NULL;
	size_t buflen = 0;
	FILE *fh = NULL;

	if ((fh = fopen(filepath, "r")) == NULL) {
		fprintf(stderr, "E: couldn't open %s: %s\n",
		    filepath, strerror(errno));
		goto err;
	}

	/* extract the filename part from the path, for nicer output */
	const char *filename = &filepath[strlen(filepath)-1];
	while (filename != filepath) {
		if (*filename == '/') {
			filename++;
			break;
		}
		filename--;
	}

	int lineno = 0;

	crypto_test_alg_t alg = ALG_NONE;
	uint64_t ntests = 0;
	crypto_test_t *test = NULL;
	uint64_t ncommitted = 0;

	char *k, *v;

	ssize_t nread;
	while ((nread = getline(&buf, &buflen, fh)) != -1 || errno == 0) {
		/* track line number for output and for test->fileloc */
		lineno++;

		if (nread < 2 && test != NULL) {
			/*
			 * blank line or end of file; close out any test in
			 * progress and commit it.
			 */
			if (test->id == 0 ||
			    test->iv.val == NULL ||
			    test->key.val == NULL ||
			    test->msg.val == NULL ||
			    test->ct.val == NULL ||
			    test->aad.val == NULL ||
			    test->tag.val == NULL ||
			    test->result == RS_NONE) {
				fprintf(stderr, "E: incomplete test [%s:%d]\n",
				    filename, lineno);
				goto err;
			}

			/* commit the test, ie, add it to the list */
			if (tail == NULL)
				tests = tail = test;
			else {
				tail->next = test;
				tail = test;
			}
			ncommitted++;

			test = NULL;
		}

		if (nread == -1)
			/* end of file and tests finished, done */
			break;

		if (nread < 2 && ncommitted == 0) {
			/*
			 * blank line after header, make sure the header is
			 * complete.
			 */
			if (alg == ALG_NONE || ntests == 0) {
				fprintf(stderr, "E: incomplete header "
				    "[%s:%d]\n", filename, lineno);
				goto err;
			}
		}

		if (nread < 2) {
			/*
			 * blank line and the header is committed, and no
			 * current test, so the next test will start on the
			 * next line.
			 */
			test = calloc(1, sizeof (crypto_test_t));
			int len = strlen(filename) + 10;
			test->fileloc = calloc(len, 1);
			snprintf(test->fileloc, len, "%s:%d",
			    filename, lineno+1);
			test->alg = alg;
			continue;
		}

		/*
		 * must be a k:v line. if there is a current test, then this
		 * line is part of it, otherwise it's a header line
		 */
		if (!split_kv(buf, &k, &v)) {
			fprintf(stderr, "E: malformed line [%s:%d]\n",
			    filename, lineno);
			goto err;
		}

		if (test == NULL) {
			/* no current test, so a header key */

			/*
			 * typical header:
			 *
			 * algorithm: AES-GCM
			 * tests: 316
			 */
			if (strcmp(k, "algorithm") == 0) {
				if (alg != ALG_NONE)
					goto err_dup_key;
				if (strcmp(v, "AES-GCM") == 0)
					alg = ALG_AES_GCM;
				else if (strcmp(v, "AES-CCM") == 0)
					alg = ALG_AES_CCM;
				else {
					fprintf(stderr,
					    "E: unknown algorithm [%s:%d]: "
					    "%s\n", filename, lineno, v);
					goto err;
				}
			} else if (strcmp(k, "tests") == 0) {
				if (ntests > 0)
					goto err_dup_key;
				if (!parse_num(v, &ntests)) {
					fprintf(stderr,
					    "E: invalid number of tests "
					    "[%s:%d]: %s\n", filename, lineno,
					    v);
					goto err;
				}
			} else {
				fprintf(stderr, "E: unknown header key "
				    "[%s:%d]: %s\n", filename, lineno, k);
				goto err;
			}
			continue;
		}

		/* test key */

		/*
		 * typical test:
		 *
		 * id: 48
		 * comment: Flipped bit 63 in tag
		 * flags: ModifiedTag
		 * iv: 505152535455565758595a5b
		 * key: 000102030405060708090a0b0c0d0e0f
		 * msg: 202122232425262728292a2b2c2d2e2f
		 * ct: eb156d081ed6b6b55f4612f021d87b39
		 * aad:
		 * tag: d8847dbc326a066988c77ad3863e6083
		 * result: invalid
		 */
		if (strcmp(k, "id") == 0) {
			if (test->id > 0)
				goto err_dup_key;
			if (!parse_num(v, &test->id)) {
				fprintf(stderr,
				    "E: invalid test id [%s:%d]: %s\n",
				    filename, lineno, v);
				goto err;
			}
			continue;
		} else if (strcmp(k, "comment") == 0) {
			if (test->comment != NULL)
				goto err_dup_key;
			test->comment = strdup(v);
			continue;
		} else if (strcmp(k, "flags") == 0) {
			if (test->flags != NULL)
				goto err_dup_key;
			test->flags = strdup(v);
			continue;
		} else if (strcmp(k, "result") == 0) {
			if (test->result != RS_NONE)
				goto err_dup_key;
			if (strcmp(v, "valid") == 0)
				test->result = RS_VALID;
			else if (strcmp(v, "invalid") == 0)
				test->result = RS_INVALID;
			else {
				fprintf(stderr,
				    "E: unknown test result [%s:%d]: %s\n",
				    filename, lineno, v);
				goto err;
			}
			continue;
		}

		/*
		 * for the test param keys, we set up a pointer to the right
		 * field in the test struct, and then work through that
		 * pointer.
		 */
		crypto_test_val_t *vp = NULL;
		if (strcmp(buf, "iv") == 0)
			vp = &test->iv;
		else if (strcmp(buf, "key") == 0)
			vp = &test->key;
		else if (strcmp(buf, "msg") == 0)
			vp = &test->msg;
		else if (strcmp(buf, "ct") == 0)
			vp = &test->ct;
		else if (strcmp(buf, "aad") == 0)
			vp = &test->aad;
		else if (strcmp(buf, "tag") == 0)
			vp = &test->tag;
		else {
			fprintf(stderr, "E: unknown key [%s:%d]: %s\n",
			    filename, lineno, buf);
			goto err;
		}

		if (vp->val != NULL)
			goto err_dup_key;

		/* sanity; these are hex bytes so must be two chars per byte. */
		size_t vlen = strlen(v);
		if ((vlen & 1) == 1) {
			fprintf(stderr, "E: value length not even "
			    "[%s:%d]: %s\n", filename, lineno, buf);
			goto err;
		}

		/*
		 * zero-length params are allowed, but ICP requires a non-NULL
		 * value pointer, so we give it one and also use that as
		 * a marker for us to know that we've filled this value.
		 */
		if (vlen == 0) {
			vp->val = val_empty;
			continue;
		}

		/*
		 * convert incoming value from hex to raw. allocate space
		 * half as long as the length, then loop the chars and
		 * convert from ascii to 4-bit values, shifting or or-ing
		 * as appropriate.
		 */
		vp->len = vlen/2;
		vp->val = calloc(vp->len, 1);

		for (int i = 0; i < vlen; i++) {
			char c = v[i];
			if (!((c >= '0' && c <= '9') ||
			    (c >= 'a' && c <= 'f'))) {
				fprintf(stderr, "E: invalid hex char "
				    "[%s:%d]: %c\n", filename, lineno, c);
				goto err;
			}

			uint8_t n = ((c <= '9') ? (c-0x30) : (c-0x57)) & 0xf;
			if ((i & 1) == 0)
				vp->val[i/2] = n << 4;
			else
				vp->val[i/2] |= n;
		}
	}

	if (errno != 0) {
		fprintf(stderr, "E: couldn't read %s: %s\n",
		    filepath, strerror(errno));
		goto err;
	}

	free(buf);
	fclose(fh);

	if (tests == NULL)
		fprintf(stderr, "E: no tests in %s\n", filepath);

	*algp = alg;
	return (tests);

/*
 * jump target for duplicate key error. this is so common that it's easier
 * to just have a single error point.
 */
err_dup_key:
	fprintf(stderr, "E: duplicate key [%s:%d]: %s\n", filename, lineno, k);

err:
	if (buf != NULL)
		free(buf);
	if (fh != NULL)
		fclose(fh);

	/*
	 * XXX we should probably free all the tests here, but the test file
	 *     is generated and this is a one-shot program, so it's really
	 *     not worth the effort today
	 */

	return (NULL);
}

/* ========== */

/* ICP algorithm implementation selection */

/*
 * It's currently not really possible to query the ICP for which
 * implementations it supports. Also, not all GCM implementations work
 * with all AES implementations. For now, we keep a hardcoded list of
 * valid combinations.
 */
static const char *aes_impl[] = {
	"generic",
	"x86_64",
	"aesni",
};

static const char *aes_gcm_impl[][2] = {
	{ "generic", "generic" },
	{ "x86_64",  "generic" },
	{ "aesni",   "generic" },
	{ "generic", "pclmulqdq" },
	{ "x86_64",  "pclmulqdq" },
	{ "aesni",   "pclmulqdq" },
	{ "x86_64",  "avx" },
	{ "aesni",   "avx" },
	{ "x86_64",  "avx2" },
	{ "aesni",   "avx2" },
};

/* signature of function to call after setting implementation params */
typedef void (*alg_cb_t)(const char *alginfo, void *arg);

/* loop over each AES-CCM implementation */
static void
foreach_aes_ccm(alg_cb_t cb, void *arg, crypto_test_outmode_t outmode)
{
	char alginfo[64];

	for (int i = 0; i < ARRAY_SIZE(aes_impl); i++) {
		snprintf(alginfo, sizeof (alginfo), "AES-CCM [%s]",
		    aes_impl[i]);

		int err = -aes_impl_set(aes_impl[i]);
		if (err != 0 && outmode != OUT_SUMMARY)
			printf("W: %s couldn't enable AES impl '%s': %s\n",
			    alginfo, aes_impl[i], strerror(err));

		cb(alginfo, (err == 0) ? arg : NULL);
	}
}

/* loop over each AES-GCM implementation */
static void
foreach_aes_gcm(alg_cb_t cb, void *arg, crypto_test_outmode_t outmode)
{
	char alginfo[64];

	for (int i = 0; i < ARRAY_SIZE(aes_gcm_impl); i++) {
		const char *aes_impl = aes_gcm_impl[i][0];
		const char *gcm_impl = aes_gcm_impl[i][1];

		snprintf(alginfo, sizeof (alginfo), "AES-GCM [%s+%s]",
		    aes_impl, gcm_impl);

		int err = -aes_impl_set(aes_impl);
		if (err != 0 && outmode != OUT_SUMMARY)
			printf("W: %s couldn't enable AES impl '%s': %s\n",
			    alginfo, aes_impl, strerror(err));

		if (err == 0) {
			err = -gcm_impl_set(gcm_impl);
			if (err != 0 && outmode != OUT_SUMMARY) {
				printf("W: %s couldn't enable "
				    "GCM impl '%s': %s\n",
				    alginfo, gcm_impl, strerror(err));
			}
		}

		cb(alginfo, (err == 0) ? arg : NULL);
	}
}

/* ========== */

/* ICP lowlevel drivers */

/*
 * initialise the mechanism (algorithm description) with the wanted parameters
 * for the next operation.
 *
 * mech must be allocated and mech->cm_params point to space large enough
 * to hold the parameters for the given algorithm.
 *
 * decrypt is true if setting up for decryption, false for encryption.
 */
static void
init_mech(crypto_mechanism_t *mech, crypto_test_alg_t alg,
    uint8_t *iv, size_t ivlen,
    uint8_t *aad, size_t aadlen,
    size_t msglen, size_t taglen,
    boolean_t decrypt)
{
	switch (alg) {
	case ALG_AES_GCM: {
		mech->cm_type = crypto_mech2id(SUN_CKM_AES_GCM);
		mech->cm_param_len = sizeof (CK_AES_GCM_PARAMS);
		CK_AES_GCM_PARAMS *p = (CK_AES_GCM_PARAMS *)mech->cm_param;
		p->pIv = (uchar_t *)iv;
		p->ulIvLen = ivlen;
		p->ulIvBits = ivlen << 3;
		p->pAAD = aad;
		p->ulAADLen = aadlen;
		p->ulTagBits = taglen << 3;
		break;
	}
	case ALG_AES_CCM: {
		mech->cm_type = crypto_mech2id(SUN_CKM_AES_CCM);
		mech->cm_param_len = sizeof (CK_AES_CCM_PARAMS);
		CK_AES_CCM_PARAMS *p = (CK_AES_CCM_PARAMS *)mech->cm_param;
		p->nonce = iv;
		p->ulNonceSize = ivlen;
		p->authData = aad;
		p->ulAuthDataSize = aadlen;
		p->ulMACSize = taglen;
		/*
		 * ICP CCM needs the MAC len in the data size for decrypt,
		 * even if the buffer isn't that big.
		 */
		p->ulDataSize = msglen + (decrypt ? taglen : 0);
		break;
	}
	default:
		abort();
	}
}

/*
 * call crypto_encrypt() with the given inputs.
 *
 *        mech: previously initialised by init_mech
 * key, keylen: raw data and length of key
 * msg, msglen: raw data and length of message
 * out, outlen: buffer to write output to (min msglen+taglen)
 *       usecp: if not NULL, recieves microseconds in crypto_encrypt()
 */
static int
encrypt_one(crypto_mechanism_t *mech,
    const uint8_t *key, size_t keylen,
    const uint8_t *msg, size_t msglen,
    uint8_t *out, size_t outlen,
    uint64_t *usecp)
{
	crypto_key_t k = {
		.ck_data = (uint8_t *)key,
		.ck_length = keylen << 3,
	};

	crypto_data_t i = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = msglen,
		.cd_raw = {
			.iov_base = (char *)msg,
			.iov_len = msglen,
		},
	};

	crypto_data_t o = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = outlen,
		.cd_raw = {
			.iov_base = (char *)out,
			.iov_len = outlen,
		},
	};

	struct timeval start, end, diff;
	if (usecp != NULL)
		gettimeofday(&start, NULL);

	int rv = crypto_encrypt(mech, &i, &k, NULL, &o);

	if (usecp != NULL) {
		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		*usecp =
		    ((uint64_t)diff.tv_sec) * 1000000 + (uint64_t)diff.tv_usec;
	}

	return (rv);
}

/*
 * call crypto_decrypt() with the given inputs.
 *
 *        mech: previously initialised by init_mech
 * key, keylen: raw data and length of key
 *   ct, ctlen: raw data and length of ciphertext
 * tag, taglen: raw data and length of tag (MAC)
 * out, outlen: buffer to write output to (min ctlen)
 *       usecp: if not NULL, recieves microseconds in crypto_decrypt()
 */
static int
decrypt_one(crypto_mechanism_t *mech,
    const uint8_t *key, size_t keylen,
    const uint8_t *ct, size_t ctlen,
    const uint8_t *tag, size_t taglen,
    uint8_t *out, size_t outlen,
    uint64_t *usecp)
{
	uint8_t inbuf[1024];

	crypto_key_t k = {
		.ck_data = (uint8_t *)key,
		.ck_length = keylen << 3,
	};

	memcpy(inbuf, ct, ctlen);
	memcpy(inbuf + ctlen, tag, taglen);
	crypto_data_t i = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = ctlen + taglen,
		.cd_raw = {
			.iov_base = (char *)inbuf,
			.iov_len = ctlen + taglen,
		},
	};

	crypto_data_t o = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = outlen,
		.cd_raw = {
			.iov_base = (char *)out,
			.iov_len = outlen
		},
	};

	struct timeval start, end, diff;
	if (usecp != NULL)
		gettimeofday(&start, NULL);

	int rv = crypto_decrypt(mech, &i, &k, NULL, &o);

	if (usecp != NULL) {
		gettimeofday(&end, NULL);
		timersub(&start, &end, &diff);
		*usecp =
		    ((uint64_t)diff.tv_sec) * 1000000 + (uint64_t)diff.tv_usec;
	}

	return (rv);
}

/* ========== */

/* correctness tests */

/*
 * helper; dump the provided data as hex, with a string prefix
 */
static void
hexdump(const char *str, const uint8_t *src, uint_t len)
{
	printf("%12s:", str);
	int i = 0;
	while (i < len) {
		if (i % 4 == 0)
			printf(" ");
		printf("%02x", src[i]);
		i++;
		if (i % 16 == 0 && i < len) {
			printf("\n");
			if (i < len)
				printf("             ");
		}
	}
	printf("\n");
}

/*
 * analyse test result and on failure, print useful output for debugging.
 *
 * test: the test we ran
 * encrypt_rv: return value from crypto_encrypt()
 * encrypt_buf: the output buffer from crypto_encrypt()
 * decrypt_rv: return value from crypto_decrypt()
 * decrypt_buf: the output buffer from crypto_decrypt()
 * outmode: output mode (summary, fail, all)
 */
static boolean_t
test_result(const crypto_test_t *test, int encrypt_rv, uint8_t *encrypt_buf,
    int decrypt_rv, uint8_t *decrypt_buf, crypto_test_outmode_t outmode)
{
	boolean_t ct_match = B_FALSE, tag_match = B_FALSE, msg_match = B_FALSE;
	boolean_t encrypt_pass = B_FALSE, decrypt_pass = B_FALSE;
	boolean_t pass = B_FALSE;

	/* check if the encrypt output matches the expected ciphertext */
	if (memcmp(encrypt_buf, test->ct.val, test->msg.len) == 0)
		ct_match = B_TRUE;

	/*
	 * check if the tag at the end of the encrypt output matches the
	 * expected tag
	 */
	if (memcmp(encrypt_buf + test->msg.len, test->tag.val,
	    test->tag.len) == 0)
		tag_match = B_TRUE;

	/* check if the decrypt output matches the expected plaintext */
	if (memcmp(decrypt_buf, test->msg.val, test->msg.len) == 0)
		msg_match = B_TRUE;

	if (test->result == RS_VALID) {
		/*
		 * a "valid" test is where the params describe an
		 * encrypt/decrypt cycle that should succeed. we consider
		 * these to have passed the test if crypto_encrypt() and
		 * crypto_decrypt() return success, and the output data
		 * matches the expected values from the test params.
		 */
		if (encrypt_rv == CRYPTO_SUCCESS) {
			if (ct_match && tag_match)
				encrypt_pass = B_TRUE;
		}
		if (decrypt_rv == CRYPTO_SUCCESS) {
			if (msg_match)
				decrypt_pass = B_TRUE;
		}
	} else {
		/*
		 * an "invalid" test is where the params describe an
		 * encrypt/decrypt cycle that should _not_ succeed.
		 *
		 * for decrypt, we only need to check the result from
		 * crypto_decrypt(), because decrypt checks the the tag (MAC)
		 * as part of its operation.
		 *
		 * for encrypt, the tag (MAC) is an output of the encryption
		 * function, so if encryption succeeds, we have to check that
		 * the returned tag matches the expected tag.
		 */
		if (encrypt_rv != CRYPTO_SUCCESS || !tag_match)
			encrypt_pass = B_TRUE;
		if (decrypt_rv != CRYPTO_SUCCESS)
			decrypt_pass = B_TRUE;
	}

	/* the test as a whole passed if both encrypt and decrypt passed */
	pass = (encrypt_pass && decrypt_pass);

	/* if the test passed we may not have to output anything */
	if (outmode == OUT_SUMMARY || (outmode == OUT_FAIL && pass))
		return (pass);

	/* print summary of test result */
	printf("%s[%lu]: encrypt=%s decrypt=%s\n", test->fileloc, test->id,
	    encrypt_pass ? "PASS" : "FAIL",
	    decrypt_pass ? "PASS" : "FAIL");

	if (!pass) {
		/*
		 * if the test didn't pass, print any comment or flags field
		 * from the test params, which if present can help
		 * understanding what the ICP did wrong
		 */
		if (test->comment != NULL)
			printf("  comment: %s\n", test->comment);
		if (test->flags != NULL)
			printf("  flags: %s\n", test->flags);
	}

	if (!encrypt_pass) {
		/* encrypt failed */

		/* print return value from crypto_encrypt() */
		printf("  encrypt rv = 0x%02x [%s]\n", encrypt_rv,
		    crypto_errstr[encrypt_rv] ?
		    crypto_errstr[encrypt_rv] : "???");

		/* print mismatched ciphertext */
		if (!ct_match) {
			printf("  ciphertexts don't match:\n");
			hexdump("got", encrypt_buf, test->msg.len);
			hexdump("expected", test->ct.val, test->msg.len);
		}

		/* print mistmatched tag (MAC) */
		if (!tag_match) {
			printf("  tags don't match:\n");
			hexdump("got", encrypt_buf + test->msg.len,
			    test->tag.len);
			hexdump("expected", test->tag.val, test->tag.len);
		}
	}

	if (!decrypt_pass) {
		/* decrypt failed */

		/* print return value from crypto_decrypt() */
		printf("  decrypt rv = 0x%02x [%s]\n", decrypt_rv,
		    crypto_errstr[decrypt_rv] ?
		    crypto_errstr[decrypt_rv] : "???");

		/* print mismatched plaintext */
		if (!msg_match) {
			printf("  plaintexts don't match:\n");
			hexdump("got", decrypt_buf, test->msg.len);
			hexdump("expected", test->msg.val, test->msg.len);
		}
	}

	if (!pass)
		printf("\n");

	return (pass);
}

/*
 * run the given list of tests.
 *
 * alginfo: a prefix for the test summary, showing the ICP algo implementation
 *          in use for this run.
 *   tests: first test in test list
 * outmode: output mode, passed to test_result()
 */
static int
run_tests(const char *alginfo, const crypto_test_t *tests,
    crypto_test_outmode_t outmode)
{
	int ntests = 0, npass = 0;

	/*
	 * allocate space for the mechanism description, and alg-specific
	 * params, and hook them up.
	 */
	crypto_mechanism_t mech = {};
	union {
		CK_AES_GCM_PARAMS gcm;
		CK_AES_CCM_PARAMS ccm;
	} params = {};
	mech.cm_param = (caddr_t)&params;

	/* space for encrypt/decrypt output */
	uint8_t encrypt_buf[1024];
	uint8_t decrypt_buf[1024];

	for (const crypto_test_t *test = tests; test != NULL;
	    test = test->next) {
		ntests++;

		/* setup mechanism description for encrypt, then encrypt */
		init_mech(&mech, test->alg, test->iv.val, test->iv.len,
		    test->aad.val, test->aad.len, test->msg.len, test->tag.len,
		    B_FALSE);
		int encrypt_rv = encrypt_one(&mech,
		    test->key.val, test->key.len,
		    test->msg.val, test->msg.len,
		    encrypt_buf, test->msg.len + test->tag.len, NULL);

		/* setup mechanism description for decrypt, then decrypt */
		init_mech(&mech, test->alg, test->iv.val, test->iv.len,
		    test->aad.val, test->aad.len, test->msg.len, test->tag.len,
		    B_TRUE);
		int decrypt_rv = decrypt_one(&mech,
		    test->key.val, test->key.len,
		    test->ct.val, test->ct.len,
		    test->tag.val, test->tag.len,
		    decrypt_buf, test->ct.len, NULL);

		/* consider results and if it passed, count it */
		if (test_result(test, encrypt_rv, encrypt_buf,
		    decrypt_rv, decrypt_buf, outmode))
			npass++;
	}

	printf("%s: tests=%d: passed=%d failed=%d\n",
	    alginfo, ntests, npass, ntests-npass);

	return (ntests != npass);
}

/* args for run_test_alg_cb */
typedef struct {
	crypto_test_t		*tests;
	crypto_test_outmode_t	outmode;
	int			failed;
} run_test_alg_args_t;

/* per-alg-impl function for correctness test runs */
static void
run_test_alg_cb(const char *alginfo, void *arg)
{
	if (arg == NULL) {
		printf("%s: [not supported on this platform]\n", alginfo);
		return;
	}
	run_test_alg_args_t *args = arg;
	args->failed += run_tests(alginfo, args->tests, args->outmode);
}

/* main function for correctness tests */
static int
runtests_main(const char *filename, crypto_test_outmode_t outmode)
{
	crypto_test_alg_t alg = ALG_NONE;
	crypto_test_t *tests = load_tests(filename, &alg);
	if (tests == NULL)
		return (1);

	icp_init();

	run_test_alg_args_t args = {
		.tests = tests,
		.outmode = outmode,
		.failed = 0,
	};

	switch (alg) {
	case ALG_AES_CCM:
		foreach_aes_ccm(run_test_alg_cb, &args, outmode);
		break;
	case ALG_AES_GCM:
		foreach_aes_gcm(run_test_alg_cb, &args, outmode);
		break;
	default:
		abort();
	}

	icp_fini();

	return (args.failed);
}

/* ========== */

/* performance tests */

/* helper; fill the given buffer with random data */
static int
fill_random(uint8_t *v, size_t sz)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return (errno);

	while (sz > 0) {
		ssize_t r = read(fd, v, sz);
		if (r < 0) {
			close(fd);
			return (errno);
		}
		v += r;
		sz -= r;
	}

	close(fd);

	return (0);
}

/* args for perf_alg_cb */
typedef struct {
	crypto_test_alg_t	alg;
	uint8_t			*msg;
	uint8_t			*out;
	uint8_t			key[32];
	uint8_t			iv[12];
} perf_alg_args_t;

#define	PERF_MSG_SHIFT_MIN	(10)	/* min test size 2^10 == 1K */
#define	PERF_MSG_SHIFT_MAX	(19)	/* max test size 2^19 == 512K */
#define	PERF_ROUNDS		(1000)	/* 1000 rounds per test */

/* per-alg-impl function for performance test runs */
static void
perf_alg_cb(const char *alginfo, void *arg)
{
	char buf[10];
	printf("%-28s", alginfo);

	if (arg == NULL) {
		printf("[not supported on this platform]\n");
		return;
	}

	perf_alg_args_t *args = arg;

	/* space for mechanism description */
	crypto_mechanism_t mech = {};
	union {
		CK_AES_GCM_PARAMS gcm;
		CK_AES_CCM_PARAMS ccm;
	} params = {};
	mech.cm_param = (caddr_t)&params;

	/* loop for each power-2 input size */
	for (int i = PERF_MSG_SHIFT_MIN; i <= PERF_MSG_SHIFT_MAX; i++) {
		/* size of input */
		size_t sz = 1<<i;

		/* initialise mechanism */
		init_mech(&mech, args->alg, args->iv, sizeof (args->iv),
		    val_empty, 0, sz, 16, B_FALSE);

		/* run N rounds and accumulate total time */
		uint64_t total = 0;
		for (int round = 0; round < PERF_ROUNDS; round++) {
			uint64_t usec;
			encrypt_one(&mech, args->key, sizeof (args->key),
			    args->msg, sz, args->out, sz+16, &usec);
			total += usec;
		}

		/*
		 * print avg time per round. zfs_nicetime expects nanoseconds,
		 * so we multiply first
		 */
		zfs_nicetime((total*1000)/PERF_ROUNDS, buf, sizeof (buf));
		printf("  %5s", buf);
	}

	printf("\n");
}

/* main function for performance tests */
static int
perf_main(const char *algname, crypto_test_outmode_t outmode)
{
	perf_alg_args_t args;

	if (strcmp(algname, "AES-CCM") == 0)
		args.alg = ALG_AES_CCM;
	else if (strcmp(algname, "AES-GCM") == 0)
		args.alg = ALG_AES_GCM;
	else {
		fprintf(stderr, "E: unknown algorithm: %s\n", algname);
		return (1);
	}

	/*
	 * test runs are often slow, but the very first ones won't be. by
	 * disabling buffering, we can display results immediately, and
	 * the user quickly gets an idea of what to expect
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* allocate random data for encrypt input */
	size_t maxsz = (1<<PERF_MSG_SHIFT_MAX);
	args.msg = malloc(maxsz);
	VERIFY0(fill_random(args.msg, maxsz));

	/* allocate space for output, +16 bytes for tag */
	args.out = malloc(maxsz+16);

	/* fill key and iv */
	VERIFY0(fill_random(args.key, sizeof (args.key)));
	VERIFY0(fill_random(args.iv, sizeof (args.iv)));

	icp_init();

	/* print header */
	char buf[10];
	printf("avg encrypt (%4d rounds)   ", PERF_ROUNDS);
	for (int i = PERF_MSG_SHIFT_MIN; i <= PERF_MSG_SHIFT_MAX; i++) {
		zfs_nicebytes(1<<i, buf, sizeof (buf));
		printf("  %5s", buf);
	}
	printf("\n");

	/* loop over all implementations of the wanted algorithm */
	switch (args.alg) {
	case ALG_AES_CCM:
		foreach_aes_ccm(perf_alg_cb, &args, outmode);
		break;
	case ALG_AES_GCM:
		foreach_aes_gcm(perf_alg_cb, &args, outmode);
		break;
	default:
		abort();
	}

	icp_fini();

	return (0);
}

/* ========== */

/* main entry */

static void
usage(void)
{
	fprintf(stderr,
	    "usage: crypto_test [-v] < -c <testfile> | -p <alg> >\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	crypto_test_outmode_t outmode = OUT_SUMMARY;
	const char *filename = NULL;
	const char *algname = NULL;

	int c;
	while ((c = getopt(argc, argv, "c:p:v")) != -1) {
		switch (c) {
		case 'c':
			filename = optarg;
			break;
		case 'p':
			algname = optarg;
			break;
		case 'v':
			outmode = (outmode == OUT_SUMMARY) ? OUT_FAIL : OUT_ALL;
			break;
		case '?':
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (filename != NULL && algname != NULL) {
		fprintf(stderr, "E: can't use -c and -p together\n");
		usage();
	}

	if (argc != 0)
		usage();

	if (filename)
		return (runtests_main(filename, outmode));

	return (perf_main(algname, outmode));
}
