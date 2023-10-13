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
 * Copyright 2020 Joyent, Inc.
 */

#include <sys/zfs_context.h>
#include <sys/fs/zfs.h>
#include <sys/dsl_crypt.h>
#include <libintl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <openssl/evp.h>
#if LIBFETCH_DYNAMIC
#include <dlfcn.h>
#endif
#if LIBFETCH_IS_FETCH
#include <sys/param.h>
#include <stdio.h>
#include <fetch.h>
#elif LIBFETCH_IS_LIBCURL
#include <curl/curl.h>
#endif
#include <libzfs.h>
#include "libzfs_impl.h"
#include "zfeature_common.h"

/*
 * User keys are used to decrypt the master encryption keys of a dataset. This
 * indirection allows a user to change his / her access key without having to
 * re-encrypt the entire dataset. User keys can be provided in one of several
 * ways. Raw keys are simply given to the kernel as is. Similarly, hex keys
 * are converted to binary and passed into the kernel. Password based keys are
 * a bit more complicated. Passwords alone do not provide suitable entropy for
 * encryption and may be too short or too long to be used. In order to derive
 * a more appropriate key we use a PBKDF2 function. This function is designed
 * to take a (relatively) long time to calculate in order to discourage
 * attackers from guessing from a list of common passwords. PBKDF2 requires
 * 2 additional parameters. The first is the number of iterations to run, which
 * will ultimately determine how long it takes to derive the resulting key from
 * the password. The second parameter is a salt that is randomly generated for
 * each dataset. The salt is used to "tweak" PBKDF2 such that a group of
 * attackers cannot reasonably generate a table of commonly known passwords to
 * their output keys and expect it work for all past and future PBKDF2 users.
 * We store the salt as a hidden property of the dataset (although it is
 * technically ok if the salt is known to the attacker).
 */

#define	MIN_PASSPHRASE_LEN 8
#define	MAX_PASSPHRASE_LEN 512
#define	MAX_KEY_PROMPT_ATTEMPTS 3

static int caught_interrupt;

static int get_key_material_file(libzfs_handle_t *, const char *, const char *,
    zfs_keyformat_t, boolean_t, uint8_t **, size_t *);
static int get_key_material_https(libzfs_handle_t *, const char *, const char *,
    zfs_keyformat_t, boolean_t, uint8_t **, size_t *);

static zfs_uri_handler_t uri_handlers[] = {
	{ "file", get_key_material_file },
	{ "https", get_key_material_https },
	{ "http", get_key_material_https },
	{ NULL, NULL }
};

static int
pkcs11_get_urandom(uint8_t *buf, size_t bytes)
{
	int rand;
	ssize_t bytes_read = 0;

	rand = open("/dev/urandom", O_RDONLY | O_CLOEXEC);

	if (rand < 0)
		return (rand);

	while (bytes_read < bytes) {
		ssize_t rc = read(rand, buf + bytes_read, bytes - bytes_read);
		if (rc < 0)
			break;
		bytes_read += rc;
	}

	(void) close(rand);

	return (bytes_read);
}

static int
zfs_prop_parse_keylocation(libzfs_handle_t *restrict hdl, const char *str,
    zfs_keylocation_t *restrict locp, char **restrict schemep)
{
	*locp = ZFS_KEYLOCATION_NONE;
	*schemep = NULL;

	if (strcmp("prompt", str) == 0) {
		*locp = ZFS_KEYLOCATION_PROMPT;
		return (0);
	}

	regmatch_t pmatch[2];

	if (regexec(&hdl->libzfs_urire, str, ARRAY_SIZE(pmatch),
	    pmatch, 0) == 0) {
		size_t scheme_len;

		if (pmatch[1].rm_so == -1) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Invalid URI"));
			return (EINVAL);
		}

		scheme_len = pmatch[1].rm_eo - pmatch[1].rm_so;

		*schemep = calloc(1, scheme_len + 1);
		if (*schemep == NULL) {
			int ret = errno;

			errno = 0;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Invalid URI"));
			return (ret);
		}

		(void) memcpy(*schemep, str + pmatch[1].rm_so, scheme_len);
		*locp = ZFS_KEYLOCATION_URI;
		return (0);
	}

	zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "Invalid keylocation"));
	return (EINVAL);
}

static int
hex_key_to_raw(char *hex, int hexlen, uint8_t *out)
{
	int ret, i;
	unsigned int c;

	for (i = 0; i < hexlen; i += 2) {
		if (!isxdigit(hex[i]) || !isxdigit(hex[i + 1])) {
			ret = EINVAL;
			goto error;
		}

		ret = sscanf(&hex[i], "%02x", &c);
		if (ret != 1) {
			ret = EINVAL;
			goto error;
		}

		out[i / 2] = c;
	}

	return (0);

error:
	return (ret);
}


static void
catch_signal(int sig)
{
	caught_interrupt = sig;
}

static const char *
get_format_prompt_string(zfs_keyformat_t format)
{
	switch (format) {
	case ZFS_KEYFORMAT_RAW:
		return ("raw key");
	case ZFS_KEYFORMAT_HEX:
		return ("hex key");
	case ZFS_KEYFORMAT_PASSPHRASE:
		return ("passphrase");
	default:
		/* shouldn't happen */
		return (NULL);
	}
}

/* do basic validation of the key material */
static int
validate_key(libzfs_handle_t *hdl, zfs_keyformat_t keyformat,
    const char *key, size_t keylen, boolean_t do_verify)
{
	switch (keyformat) {
	case ZFS_KEYFORMAT_RAW:
		/* verify the key length is correct */
		if (keylen < WRAPPING_KEY_LEN) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Raw key too short (expected %u)."),
			    WRAPPING_KEY_LEN);
			return (EINVAL);
		}

		if (keylen > WRAPPING_KEY_LEN) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Raw key too long (expected %u)."),
			    WRAPPING_KEY_LEN);
			return (EINVAL);
		}
		break;
	case ZFS_KEYFORMAT_HEX:
		/* verify the key length is correct */
		if (keylen < WRAPPING_KEY_LEN * 2) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Hex key too short (expected %u)."),
			    WRAPPING_KEY_LEN * 2);
			return (EINVAL);
		}

		if (keylen > WRAPPING_KEY_LEN * 2) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Hex key too long (expected %u)."),
			    WRAPPING_KEY_LEN * 2);
			return (EINVAL);
		}

		/* check for invalid hex digits */
		for (size_t i = 0; i < WRAPPING_KEY_LEN * 2; i++) {
			if (!isxdigit(key[i])) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "Invalid hex character detected."));
				return (EINVAL);
			}
		}
		break;
	case ZFS_KEYFORMAT_PASSPHRASE:
		/*
		 * Verify the length is within bounds when setting a new key,
		 * but not when loading an existing key.
		 */
		if (!do_verify)
			break;
		if (keylen > MAX_PASSPHRASE_LEN) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Passphrase too long (max %u)."),
			    MAX_PASSPHRASE_LEN);
			return (EINVAL);
		}

		if (keylen < MIN_PASSPHRASE_LEN) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Passphrase too short (min %u)."),
			    MIN_PASSPHRASE_LEN);
			return (EINVAL);
		}
		break;
	default:
		/* can't happen, checked above */
		break;
	}

	return (0);
}

static int
libzfs_getpassphrase(zfs_keyformat_t keyformat, boolean_t is_reenter,
    boolean_t new_key, const char *fsname,
    char **restrict res, size_t *restrict reslen)
{
	FILE *f = stdin;
	size_t buflen = 0;
	ssize_t bytes;
	int ret = 0;
	struct termios old_term, new_term;
	struct sigaction act, osigint, osigtstp;

	*res = NULL;
	*reslen = 0;

	/*
	 * handle SIGINT and ignore SIGSTP. This is necessary to
	 * restore the state of the terminal.
	 */
	caught_interrupt = 0;
	act.sa_flags = 0;
	(void) sigemptyset(&act.sa_mask);
	act.sa_handler = catch_signal;

	(void) sigaction(SIGINT, &act, &osigint);
	act.sa_handler = SIG_IGN;
	(void) sigaction(SIGTSTP, &act, &osigtstp);

	(void) printf("%s %s%s",
	    is_reenter ? "Re-enter" : "Enter",
	    new_key ? "new " : "",
	    get_format_prompt_string(keyformat));
	if (fsname != NULL)
		(void) printf(" for '%s'", fsname);
	(void) fputc(':', stdout);
	(void) fflush(stdout);

	/* disable the terminal echo for key input */
	(void) tcgetattr(fileno(f), &old_term);

	new_term = old_term;
	new_term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);

	ret = tcsetattr(fileno(f), TCSAFLUSH, &new_term);
	if (ret != 0) {
		ret = errno;
		errno = 0;
		goto out;
	}

	bytes = getline(res, &buflen, f);
	if (bytes < 0) {
		ret = errno;
		errno = 0;
		goto out;
	}

	/* trim the ending newline if it exists */
	if (bytes > 0 && (*res)[bytes - 1] == '\n') {
		(*res)[bytes - 1] = '\0';
		bytes--;
	}

	*reslen = bytes;

out:
	/* reset the terminal */
	(void) tcsetattr(fileno(f), TCSAFLUSH, &old_term);
	(void) sigaction(SIGINT, &osigint, NULL);
	(void) sigaction(SIGTSTP, &osigtstp, NULL);

	/* if we caught a signal, re-throw it now */
	if (caught_interrupt != 0)
		(void) kill(getpid(), caught_interrupt);

	/* print the newline that was not echo'd */
	(void) printf("\n");

	return (ret);
}

static int
get_key_interactive(libzfs_handle_t *restrict hdl, const char *fsname,
    zfs_keyformat_t keyformat, boolean_t confirm_key, boolean_t newkey,
    uint8_t **restrict outbuf, size_t *restrict len_out)
{
	char *buf = NULL, *buf2 = NULL;
	size_t buflen = 0, buf2len = 0;
	int ret = 0;

	ASSERT(isatty(fileno(stdin)));

	/* raw keys cannot be entered on the terminal */
	if (keyformat == ZFS_KEYFORMAT_RAW) {
		ret = EINVAL;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Cannot enter raw keys on the terminal"));
		goto out;
	}

	/* prompt for the key */
	if ((ret = libzfs_getpassphrase(keyformat, B_FALSE, newkey, fsname,
	    &buf, &buflen)) != 0) {
		free(buf);
		buf = NULL;
		buflen = 0;
		goto out;
	}

	if (!confirm_key)
		goto out;

	if ((ret = validate_key(hdl, keyformat, buf, buflen, confirm_key)) !=
	    0) {
		free(buf);
		return (ret);
	}

	ret = libzfs_getpassphrase(keyformat, B_TRUE, newkey, fsname, &buf2,
	    &buf2len);
	if (ret != 0) {
		free(buf);
		free(buf2);
		buf = buf2 = NULL;
		buflen = buf2len = 0;
		goto out;
	}

	if (buflen != buf2len || strcmp(buf, buf2) != 0) {
		free(buf);
		buf = NULL;
		buflen = 0;

		ret = EINVAL;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Provided keys do not match."));
	}

	free(buf2);

out:
	*outbuf = (uint8_t *)buf;
	*len_out = buflen;
	return (ret);
}

static int
get_key_material_raw(FILE *fd, zfs_keyformat_t keyformat,
    uint8_t **buf, size_t *len_out)
{
	int ret = 0;
	size_t buflen = 0;

	*len_out = 0;

	/* read the key material */
	if (keyformat != ZFS_KEYFORMAT_RAW) {
		ssize_t bytes;

		bytes = getline((char **)buf, &buflen, fd);
		if (bytes < 0) {
			ret = errno;
			errno = 0;
			goto out;
		}

		/* trim the ending newline if it exists */
		if (bytes > 0 && (*buf)[bytes - 1] == '\n') {
			(*buf)[bytes - 1] = '\0';
			bytes--;
		}

		*len_out = bytes;
	} else {
		size_t n;

		/*
		 * Raw keys may have newline characters in them and so can't
		 * use getline(). Here we attempt to read 33 bytes so that we
		 * can properly check the key length (the file should only have
		 * 32 bytes).
		 */
		*buf = malloc((WRAPPING_KEY_LEN + 1) * sizeof (uint8_t));
		if (*buf == NULL) {
			ret = ENOMEM;
			goto out;
		}

		n = fread(*buf, 1, WRAPPING_KEY_LEN + 1, fd);
		if (n == 0 || ferror(fd)) {
			/* size errors are handled by the calling function */
			free(*buf);
			*buf = NULL;
			ret = errno;
			errno = 0;
			goto out;
		}

		*len_out = n;
	}
out:
	return (ret);
}

static int
get_key_material_file(libzfs_handle_t *hdl, const char *uri,
    const char *fsname, zfs_keyformat_t keyformat, boolean_t newkey,
    uint8_t **restrict buf, size_t *restrict len_out)
{
	(void) fsname, (void) newkey;
	FILE *f = NULL;
	int ret = 0;

	if (strlen(uri) < 7)
		return (EINVAL);

	if ((f = fopen(uri + 7, "re")) == NULL) {
		ret = errno;
		errno = 0;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Failed to open key material file: %s"), strerror(ret));
		return (ret);
	}

	ret = get_key_material_raw(f, keyformat, buf, len_out);

	(void) fclose(f);

	return (ret);
}

static int
get_key_material_https(libzfs_handle_t *hdl, const char *uri,
    const char *fsname, zfs_keyformat_t keyformat, boolean_t newkey,
    uint8_t **restrict buf, size_t *restrict len_out)
{
	(void) fsname, (void) newkey;
	int ret = 0;
	FILE *key = NULL;
	boolean_t is_http = strncmp(uri, "http:", strlen("http:")) == 0;

	if (strlen(uri) < (is_http ? 7 : 8)) {
		ret = EINVAL;
		goto end;
	}

#if LIBFETCH_DYNAMIC
#define	LOAD_FUNCTION(func) \
	__typeof__(func) *func = dlsym(hdl->libfetch, #func);

	if (hdl->libfetch == NULL)
		hdl->libfetch = dlopen(LIBFETCH_SONAME, RTLD_LAZY);

	if (hdl->libfetch == NULL) {
		hdl->libfetch = (void *)-1;
		char *err = dlerror();
		if (err)
			hdl->libfetch_load_error = strdup(err);
	}

	if (hdl->libfetch == (void *)-1) {
		ret = ENOSYS;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Couldn't load %s: %s"),
		    LIBFETCH_SONAME, hdl->libfetch_load_error ?: "(?)");
		goto end;
	}

	boolean_t ok;
#if LIBFETCH_IS_FETCH
	LOAD_FUNCTION(fetchGetURL);
	char *fetchLastErrString = dlsym(hdl->libfetch, "fetchLastErrString");

	ok = fetchGetURL && fetchLastErrString;
#elif LIBFETCH_IS_LIBCURL
	LOAD_FUNCTION(curl_easy_init);
	LOAD_FUNCTION(curl_easy_setopt);
	LOAD_FUNCTION(curl_easy_perform);
	LOAD_FUNCTION(curl_easy_cleanup);
	LOAD_FUNCTION(curl_easy_strerror);
	LOAD_FUNCTION(curl_easy_getinfo);

	ok = curl_easy_init && curl_easy_setopt && curl_easy_perform &&
	    curl_easy_cleanup && curl_easy_strerror && curl_easy_getinfo;
#endif
	if (!ok) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "keylocation=%s back-end %s missing symbols."),
		    is_http ? "http://" : "https://", LIBFETCH_SONAME);
		ret = ENOSYS;
		goto end;
	}
#endif

#if LIBFETCH_IS_FETCH
	key = fetchGetURL(uri, "");
	if (key == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Couldn't GET %s: %s"),
		    uri, fetchLastErrString);
		ret = ENETDOWN;
	}
#elif LIBFETCH_IS_LIBCURL
	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		ret = ENOTSUP;
		goto end;
	}

	int kfd = -1;
#ifdef O_TMPFILE
	kfd = open(getenv("TMPDIR") ?: "/tmp",
	    O_RDWR | O_TMPFILE | O_EXCL | O_CLOEXEC, 0600);
	if (kfd != -1)
		goto kfdok;
#endif

	char *path;
	if (asprintf(&path,
	    "%s/libzfs-XXXXXXXX.https", getenv("TMPDIR") ?: "/tmp") == -1) {
		ret = ENOMEM;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s"),
		    strerror(ret));
		goto end;
	}

	kfd = mkostemps(path, strlen(".https"), O_CLOEXEC);
	if (kfd == -1) {
		ret = errno;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Couldn't create temporary file %s: %s"),
		    path, strerror(ret));
		free(path);
		goto end;
	}
	(void) unlink(path);
	free(path);

kfdok:
	if ((key = fdopen(kfd, "r+")) == NULL) {
		ret = errno;
		(void) close(kfd);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Couldn't reopen temporary file: %s"), strerror(ret));
		goto end;
	}

	char errbuf[CURL_ERROR_SIZE] = "";
	char *cainfo = getenv("SSL_CA_CERT_FILE"); /* matches fetch(3) */
	char *capath = getenv("SSL_CA_CERT_PATH"); /* matches fetch(3) */
	char *clcert = getenv("SSL_CLIENT_CERT_FILE"); /* matches fetch(3) */
	char *clkey  = getenv("SSL_CLIENT_KEY_FILE"); /* matches fetch(3) */
	(void) curl_easy_setopt(curl, CURLOPT_URL, uri);
	(void) curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	(void) curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
	(void) curl_easy_setopt(curl, CURLOPT_WRITEDATA, key);
	(void) curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	if (cainfo != NULL)
		(void) curl_easy_setopt(curl, CURLOPT_CAINFO, cainfo);
	if (capath != NULL)
		(void) curl_easy_setopt(curl, CURLOPT_CAPATH, capath);
	if (clcert != NULL)
		(void) curl_easy_setopt(curl, CURLOPT_SSLCERT, clcert);
	if (clkey != NULL)
		(void) curl_easy_setopt(curl, CURLOPT_SSLKEY, clkey);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Failed to connect to %s: %s"),
		    uri, strlen(errbuf) ? errbuf : curl_easy_strerror(res));
		ret = ENETDOWN;
	} else {
		long resp = 200;
		(void) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp);

		if (resp < 200 || resp >= 300) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Couldn't GET %s: %ld"),
			    uri, resp);
			ret = ENOENT;
		} else
			rewind(key);
	}

	curl_easy_cleanup(curl);
#else
	zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
	    "No keylocation=%s back-end."), is_http ? "http://" : "https://");
	ret = ENOSYS;
#endif

end:
	if (ret == 0)
		ret = get_key_material_raw(key, keyformat, buf, len_out);

	if (key != NULL)
		fclose(key);

	return (ret);
}

/*
 * Attempts to fetch key material, no matter where it might live. The key
 * material is allocated and returned in km_out. *can_retry_out will be set
 * to B_TRUE if the user is providing the key material interactively, allowing
 * for re-entry attempts.
 */
static int
get_key_material(libzfs_handle_t *hdl, boolean_t do_verify, boolean_t newkey,
    zfs_keyformat_t keyformat, const char *keylocation, const char *fsname,
    uint8_t **km_out, size_t *kmlen_out, boolean_t *can_retry_out)
{
	int ret;
	zfs_keylocation_t keyloc = ZFS_KEYLOCATION_NONE;
	uint8_t *km = NULL;
	size_t kmlen = 0;
	char *uri_scheme = NULL;
	zfs_uri_handler_t *handler = NULL;
	boolean_t can_retry = B_FALSE;

	/* verify and parse the keylocation */
	ret = zfs_prop_parse_keylocation(hdl, keylocation, &keyloc,
	    &uri_scheme);
	if (ret != 0)
		goto error;

	/* open the appropriate file descriptor */
	switch (keyloc) {
	case ZFS_KEYLOCATION_PROMPT:
		if (isatty(fileno(stdin))) {
			can_retry = keyformat != ZFS_KEYFORMAT_RAW;
			ret = get_key_interactive(hdl, fsname, keyformat,
			    do_verify, newkey, &km, &kmlen);
		} else {
			/* fetch the key material into the buffer */
			ret = get_key_material_raw(stdin, keyformat, &km,
			    &kmlen);
		}

		if (ret != 0)
			goto error;

		break;
	case ZFS_KEYLOCATION_URI:
		ret = ENOTSUP;

		for (handler = uri_handlers; handler->zuh_scheme != NULL;
		    handler++) {
			if (strcmp(handler->zuh_scheme, uri_scheme) != 0)
				continue;

			if ((ret = handler->zuh_handler(hdl, keylocation,
			    fsname, keyformat, newkey, &km, &kmlen)) != 0)
				goto error;

			break;
		}

		if (ret == ENOTSUP) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "URI scheme is not supported"));
			goto error;
		}

		break;
	default:
		ret = EINVAL;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Invalid keylocation."));
		goto error;
	}

	if ((ret = validate_key(hdl, keyformat, (const char *)km, kmlen,
	    do_verify)) != 0)
		goto error;

	*km_out = km;
	*kmlen_out = kmlen;
	if (can_retry_out != NULL)
		*can_retry_out = can_retry;

	free(uri_scheme);
	return (0);

error:
	free(km);

	*km_out = NULL;
	*kmlen_out = 0;

	if (can_retry_out != NULL)
		*can_retry_out = can_retry;

	free(uri_scheme);
	return (ret);
}

static int
derive_key(libzfs_handle_t *hdl, zfs_keyformat_t format, uint64_t iters,
    uint8_t *key_material, uint64_t salt,
    uint8_t **key_out)
{
	int ret;
	uint8_t *key;

	*key_out = NULL;

	key = zfs_alloc(hdl, WRAPPING_KEY_LEN);

	switch (format) {
	case ZFS_KEYFORMAT_RAW:
		memcpy(key, key_material, WRAPPING_KEY_LEN);
		break;
	case ZFS_KEYFORMAT_HEX:
		ret = hex_key_to_raw((char *)key_material,
		    WRAPPING_KEY_LEN * 2, key);
		if (ret != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Invalid hex key provided."));
			goto error;
		}
		break;
	case ZFS_KEYFORMAT_PASSPHRASE:
		salt = LE_64(salt);

		ret = PKCS5_PBKDF2_HMAC_SHA1((char *)key_material,
		    strlen((char *)key_material), ((uint8_t *)&salt),
		    sizeof (uint64_t), iters, WRAPPING_KEY_LEN, key);
		if (ret != 1) {
			ret = EIO;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Failed to generate key from passphrase."));
			goto error;
		}
		break;
	default:
		ret = EINVAL;
		goto error;
	}

	*key_out = key;
	return (0);

error:
	free(key);

	*key_out = NULL;
	return (ret);
}

static boolean_t
encryption_feature_is_enabled(zpool_handle_t *zph)
{
	nvlist_t *features;
	uint64_t feat_refcount;

	/* check that features can be enabled */
	if (zpool_get_prop_int(zph, ZPOOL_PROP_VERSION, NULL)
	    < SPA_VERSION_FEATURES)
		return (B_FALSE);

	/* check for crypto feature */
	features = zpool_get_features(zph);
	if (!features || nvlist_lookup_uint64(features,
	    spa_feature_table[SPA_FEATURE_ENCRYPTION].fi_guid,
	    &feat_refcount) != 0)
		return (B_FALSE);

	return (B_TRUE);
}

static int
populate_create_encryption_params_nvlists(libzfs_handle_t *hdl,
    zfs_handle_t *zhp, boolean_t newkey, zfs_keyformat_t keyformat,
    const char *keylocation, nvlist_t *props, uint8_t **wkeydata,
    uint_t *wkeylen)
{
	int ret;
	uint64_t iters = 0, salt = 0;
	uint8_t *key_material = NULL;
	size_t key_material_len = 0;
	uint8_t *key_data = NULL;
	const char *fsname = (zhp) ? zfs_get_name(zhp) : NULL;

	/* get key material from keyformat and keylocation */
	ret = get_key_material(hdl, B_TRUE, newkey, keyformat, keylocation,
	    fsname, &key_material, &key_material_len, NULL);
	if (ret != 0)
		goto error;

	/* passphrase formats require a salt and pbkdf2 iters property */
	if (keyformat == ZFS_KEYFORMAT_PASSPHRASE) {
		/* always generate a new salt */
		ret = pkcs11_get_urandom((uint8_t *)&salt, sizeof (uint64_t));
		if (ret != sizeof (uint64_t)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Failed to generate salt."));
			goto error;
		}

		ret = nvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), salt);
		if (ret != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Failed to add salt to properties."));
			goto error;
		}

		/*
		 * If not otherwise specified, use the default number of
		 * pbkdf2 iterations. If specified, we have already checked
		 * that the given value is greater than MIN_PBKDF2_ITERATIONS
		 * during zfs_valid_proplist().
		 */
		ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), &iters);
		if (ret == ENOENT) {
			iters = DEFAULT_PBKDF2_ITERATIONS;
			ret = nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), iters);
			if (ret != 0)
				goto error;
		} else if (ret != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Failed to get pbkdf2 iterations."));
			goto error;
		}
	} else {
		/* check that pbkdf2iters was not specified by the user */
		ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), &iters);
		if (ret == 0) {
			ret = EINVAL;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Cannot specify pbkdf2iters with a non-passphrase "
			    "keyformat."));
			goto error;
		}
	}

	/* derive a key from the key material */
	ret = derive_key(hdl, keyformat, iters, key_material, salt, &key_data);
	if (ret != 0)
		goto error;

	free(key_material);

	*wkeydata = key_data;
	*wkeylen = WRAPPING_KEY_LEN;
	return (0);

error:
	if (key_material != NULL)
		free(key_material);
	if (key_data != NULL)
		free(key_data);

	*wkeydata = NULL;
	*wkeylen = 0;
	return (ret);
}

static boolean_t
proplist_has_encryption_props(nvlist_t *props)
{
	int ret;
	uint64_t intval;
	const char *strval;

	ret = nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), &intval);
	if (ret == 0 && intval != ZIO_CRYPT_OFF)
		return (B_TRUE);

	ret = nvlist_lookup_string(props,
	    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &strval);
	if (ret == 0 && strcmp(strval, "none") != 0)
		return (B_TRUE);

	ret = nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), &intval);
	if (ret == 0)
		return (B_TRUE);

	ret = nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), &intval);
	if (ret == 0)
		return (B_TRUE);

	return (B_FALSE);
}

int
zfs_crypto_get_encryption_root(zfs_handle_t *zhp, boolean_t *is_encroot,
    char *buf)
{
	int ret;
	char prop_encroot[MAXNAMELEN];

	/* if the dataset isn't encrypted, just return */
	if (zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) == ZIO_CRYPT_OFF) {
		*is_encroot = B_FALSE;
		if (buf != NULL)
			buf[0] = '\0';
		return (0);
	}

	ret = zfs_prop_get(zhp, ZFS_PROP_ENCRYPTION_ROOT, prop_encroot,
	    sizeof (prop_encroot), NULL, NULL, 0, B_TRUE);
	if (ret != 0) {
		*is_encroot = B_FALSE;
		if (buf != NULL)
			buf[0] = '\0';
		return (ret);
	}

	*is_encroot = strcmp(prop_encroot, zfs_get_name(zhp)) == 0;
	if (buf != NULL)
		strcpy(buf, prop_encroot);

	return (0);
}

int
zfs_crypto_create(libzfs_handle_t *hdl, char *parent_name, nvlist_t *props,
    nvlist_t *pool_props, boolean_t stdin_available, uint8_t **wkeydata_out,
    uint_t *wkeylen_out)
{
	int ret;
	char errbuf[ERRBUFLEN];
	uint64_t crypt = ZIO_CRYPT_INHERIT, pcrypt = ZIO_CRYPT_INHERIT;
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	const char *keylocation = NULL;
	zfs_handle_t *pzhp = NULL;
	uint8_t *wkeydata = NULL;
	uint_t wkeylen = 0;
	boolean_t local_crypt = B_TRUE;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Encryption create error"));

	/* lookup crypt from props */
	ret = nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), &crypt);
	if (ret != 0)
		local_crypt = B_FALSE;

	/* lookup key location and format from props */
	(void) nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), &keyformat);
	(void) nvlist_lookup_string(props,
	    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &keylocation);

	if (parent_name != NULL) {
		/* get a reference to parent dataset */
		pzhp = make_dataset_handle(hdl, parent_name);
		if (pzhp == NULL) {
			ret = ENOENT;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Failed to lookup parent."));
			goto out;
		}

		/* Lookup parent's crypt */
		pcrypt = zfs_prop_get_int(pzhp, ZFS_PROP_ENCRYPTION);

		/* Params require the encryption feature */
		if (!encryption_feature_is_enabled(pzhp->zpool_hdl)) {
			if (proplist_has_encryption_props(props)) {
				ret = EINVAL;
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "Encryption feature not enabled."));
				goto out;
			}

			ret = 0;
			goto out;
		}
	} else {
		/*
		 * special case for root dataset where encryption feature
		 * feature won't be on disk yet
		 */
		if (!nvlist_exists(pool_props, "feature@encryption")) {
			if (proplist_has_encryption_props(props)) {
				ret = EINVAL;
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "Encryption feature not enabled."));
				goto out;
			}

			ret = 0;
			goto out;
		}

		pcrypt = ZIO_CRYPT_OFF;
	}

	/* Get the inherited encryption property if we don't have it locally */
	if (!local_crypt)
		crypt = pcrypt;

	/*
	 * At this point crypt should be the actual encryption value. If
	 * encryption is off just verify that no encryption properties have
	 * been specified and return.
	 */
	if (crypt == ZIO_CRYPT_OFF) {
		if (proplist_has_encryption_props(props)) {
			ret = EINVAL;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Encryption must be turned on to set encryption "
			    "properties."));
			goto out;
		}

		ret = 0;
		goto out;
	}

	/*
	 * If we have a parent crypt it is valid to specify encryption alone.
	 * This will result in a child that is encrypted with the chosen
	 * encryption suite that will also inherit the parent's key. If
	 * the parent is not encrypted we need an encryption suite provided.
	 */
	if (pcrypt == ZIO_CRYPT_OFF && keylocation == NULL &&
	    keyformat == ZFS_KEYFORMAT_NONE) {
		ret = EINVAL;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Keyformat required for new encryption root."));
		goto out;
	}

	/*
	 * Specifying a keylocation implies this will be a new encryption root.
	 * Check that a keyformat is also specified.
	 */
	if (keylocation != NULL && keyformat == ZFS_KEYFORMAT_NONE) {
		ret = EINVAL;
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Keyformat required for new encryption root."));
		goto out;
	}

	/* default to prompt if no keylocation is specified */
	if (keyformat != ZFS_KEYFORMAT_NONE && keylocation == NULL) {
		keylocation = (char *)"prompt";
		ret = nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), keylocation);
		if (ret != 0)
			goto out;
	}

	/*
	 * If a local key is provided, this dataset will be a new
	 * encryption root. Populate the encryption params.
	 */
	if (keylocation != NULL) {
		/*
		 * 'zfs recv -o keylocation=prompt' won't work because stdin
		 * is being used by the send stream, so we disallow it.
		 */
		if (!stdin_available && strcmp(keylocation, "prompt") == 0) {
			ret = EINVAL;
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "Cannot use "
			    "'prompt' keylocation because stdin is in use."));
			goto out;
		}

		ret = populate_create_encryption_params_nvlists(hdl, NULL,
		    B_TRUE, keyformat, keylocation, props, &wkeydata,
		    &wkeylen);
		if (ret != 0)
			goto out;
	}

	if (pzhp != NULL)
		zfs_close(pzhp);

	*wkeydata_out = wkeydata;
	*wkeylen_out = wkeylen;
	return (0);

out:
	if (pzhp != NULL)
		zfs_close(pzhp);
	if (wkeydata != NULL)
		free(wkeydata);

	*wkeydata_out = NULL;
	*wkeylen_out = 0;
	return (ret);
}

int
zfs_crypto_clone_check(libzfs_handle_t *hdl, zfs_handle_t *origin_zhp,
    char *parent_name, nvlist_t *props)
{
	(void) origin_zhp, (void) parent_name;
	char errbuf[ERRBUFLEN];

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Encryption clone error"));

	/*
	 * No encryption properties should be specified. They will all be
	 * inherited from the origin dataset.
	 */
	if (nvlist_exists(props, zfs_prop_to_name(ZFS_PROP_KEYFORMAT)) ||
	    nvlist_exists(props, zfs_prop_to_name(ZFS_PROP_KEYLOCATION)) ||
	    nvlist_exists(props, zfs_prop_to_name(ZFS_PROP_ENCRYPTION)) ||
	    nvlist_exists(props, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS))) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Encryption properties must inherit from origin dataset."));
		return (EINVAL);
	}

	return (0);
}

typedef struct loadkeys_cbdata {
	uint64_t cb_numfailed;
	uint64_t cb_numattempted;
} loadkey_cbdata_t;

static int
load_keys_cb(zfs_handle_t *zhp, void *arg)
{
	int ret;
	boolean_t is_encroot;
	loadkey_cbdata_t *cb = arg;
	uint64_t keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);

	/* only attempt to load keys for encryption roots */
	ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, NULL);
	if (ret != 0 || !is_encroot)
		goto out;

	/* don't attempt to load already loaded keys */
	if (keystatus == ZFS_KEYSTATUS_AVAILABLE)
		goto out;

	/* Attempt to load the key. Record status in cb. */
	cb->cb_numattempted++;

	ret = zfs_crypto_load_key(zhp, B_FALSE, NULL);
	if (ret)
		cb->cb_numfailed++;

out:
	(void) zfs_iter_filesystems_v2(zhp, 0, load_keys_cb, cb);
	zfs_close(zhp);

	/* always return 0, since this function is best effort */
	return (0);
}

/*
 * This function is best effort. It attempts to load all the keys for the given
 * filesystem and all of its children.
 */
int
zfs_crypto_attempt_load_keys(libzfs_handle_t *hdl, const char *fsname)
{
	int ret;
	zfs_handle_t *zhp = NULL;
	loadkey_cbdata_t cb = { 0 };

	zhp = zfs_open(hdl, fsname, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL) {
		ret = ENOENT;
		goto error;
	}

	ret = load_keys_cb(zfs_handle_dup(zhp), &cb);
	if (ret)
		goto error;

	(void) printf(gettext("%llu / %llu keys successfully loaded\n"),
	    (u_longlong_t)(cb.cb_numattempted - cb.cb_numfailed),
	    (u_longlong_t)cb.cb_numattempted);

	if (cb.cb_numfailed != 0) {
		ret = -1;
		goto error;
	}

	zfs_close(zhp);
	return (0);

error:
	if (zhp != NULL)
		zfs_close(zhp);
	return (ret);
}

int
zfs_crypto_load_key(zfs_handle_t *zhp, boolean_t noop,
    const char *alt_keylocation)
{
	int ret, attempts = 0;
	char errbuf[ERRBUFLEN];
	uint64_t keystatus, iters = 0, salt = 0;
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	char prop_keylocation[MAXNAMELEN];
	char prop_encroot[MAXNAMELEN];
	const char *keylocation = NULL;
	uint8_t *key_material = NULL, *key_data = NULL;
	size_t key_material_len;
	boolean_t is_encroot, can_retry = B_FALSE, correctible = B_FALSE;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Key load error"));

	/* check that encryption is enabled for the pool */
	if (!encryption_feature_is_enabled(zhp->zpool_hdl)) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Encryption feature not enabled."));
		ret = EINVAL;
		goto error;
	}

	/* Fetch the keyformat. Check that the dataset is encrypted. */
	keyformat = zfs_prop_get_int(zhp, ZFS_PROP_KEYFORMAT);
	if (keyformat == ZFS_KEYFORMAT_NONE) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "'%s' is not encrypted."), zfs_get_name(zhp));
		ret = EINVAL;
		goto error;
	}

	/*
	 * Fetch the key location. Check that we are working with an
	 * encryption root.
	 */
	ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, prop_encroot);
	if (ret != 0) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Failed to get encryption root for '%s'."),
		    zfs_get_name(zhp));
		goto error;
	} else if (!is_encroot) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Keys must be loaded for encryption root of '%s' (%s)."),
		    zfs_get_name(zhp), prop_encroot);
		ret = EINVAL;
		goto error;
	}

	/*
	 * if the caller has elected to override the keylocation property
	 * use that instead
	 */
	if (alt_keylocation != NULL) {
		keylocation = alt_keylocation;
	} else {
		ret = zfs_prop_get(zhp, ZFS_PROP_KEYLOCATION, prop_keylocation,
		    sizeof (prop_keylocation), NULL, NULL, 0, B_TRUE);
		if (ret != 0) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Failed to get keylocation for '%s'."),
			    zfs_get_name(zhp));
			goto error;
		}

		keylocation = prop_keylocation;
	}

	/* check that the key is unloaded unless this is a noop */
	if (!noop) {
		keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
		if (keystatus == ZFS_KEYSTATUS_AVAILABLE) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Key already loaded for '%s'."), zfs_get_name(zhp));
			ret = EEXIST;
			goto error;
		}
	}

	/* passphrase formats require a salt and pbkdf2_iters property */
	if (keyformat == ZFS_KEYFORMAT_PASSPHRASE) {
		salt = zfs_prop_get_int(zhp, ZFS_PROP_PBKDF2_SALT);
		iters = zfs_prop_get_int(zhp, ZFS_PROP_PBKDF2_ITERS);
	}

try_again:
	/* fetching and deriving the key are correctable errors. set the flag */
	correctible = B_TRUE;

	/* get key material from key format and location */
	ret = get_key_material(zhp->zfs_hdl, B_FALSE, B_FALSE, keyformat,
	    keylocation, zfs_get_name(zhp), &key_material, &key_material_len,
	    &can_retry);
	if (ret != 0)
		goto error;

	/* derive a key from the key material */
	ret = derive_key(zhp->zfs_hdl, keyformat, iters, key_material, salt,
	    &key_data);
	if (ret != 0)
		goto error;

	correctible = B_FALSE;

	/* pass the wrapping key and noop flag to the ioctl */
	ret = lzc_load_key(zhp->zfs_name, noop, key_data, WRAPPING_KEY_LEN);
	if (ret != 0) {
		switch (ret) {
		case EPERM:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Permission denied."));
			break;
		case EINVAL:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Invalid parameters provided for dataset %s."),
			    zfs_get_name(zhp));
			break;
		case EEXIST:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Key already loaded for '%s'."), zfs_get_name(zhp));
			break;
		case EBUSY:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "'%s' is busy."), zfs_get_name(zhp));
			break;
		case EACCES:
			correctible = B_TRUE;
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Incorrect key provided for '%s'."),
			    zfs_get_name(zhp));
			break;
		case ZFS_ERR_CRYPTO_NOTSUP:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "'%s' uses an unsupported encryption suite."),
			    zfs_get_name(zhp));
			break;
		}
		goto error;
	}

	free(key_material);
	free(key_data);

	return (0);

error:
	zfs_error(zhp->zfs_hdl, EZFS_CRYPTOFAILED, errbuf);
	if (key_material != NULL) {
		free(key_material);
		key_material = NULL;
	}
	if (key_data != NULL) {
		free(key_data);
		key_data = NULL;
	}

	/*
	 * Here we decide if it is ok to allow the user to retry entering their
	 * key. The can_retry flag will be set if the user is entering their
	 * key from an interactive prompt. The correctable flag will only be
	 * set if an error that occurred could be corrected by retrying. Both
	 * flags are needed to allow the user to attempt key entry again
	 */
	attempts++;
	if (can_retry && correctible && attempts < MAX_KEY_PROMPT_ATTEMPTS)
		goto try_again;

	return (ret);
}

int
zfs_crypto_unload_key(zfs_handle_t *zhp)
{
	int ret;
	char errbuf[ERRBUFLEN];
	char prop_encroot[MAXNAMELEN];
	uint64_t keystatus, keyformat;
	boolean_t is_encroot;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Key unload error"));

	/* check that encryption is enabled for the pool */
	if (!encryption_feature_is_enabled(zhp->zpool_hdl)) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Encryption feature not enabled."));
		ret = EINVAL;
		goto error;
	}

	/* Fetch the keyformat. Check that the dataset is encrypted. */
	keyformat = zfs_prop_get_int(zhp, ZFS_PROP_KEYFORMAT);
	if (keyformat == ZFS_KEYFORMAT_NONE) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "'%s' is not encrypted."), zfs_get_name(zhp));
		ret = EINVAL;
		goto error;
	}

	/*
	 * Fetch the key location. Check that we are working with an
	 * encryption root.
	 */
	ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, prop_encroot);
	if (ret != 0) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Failed to get encryption root for '%s'."),
		    zfs_get_name(zhp));
		goto error;
	} else if (!is_encroot) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Keys must be unloaded for encryption root of '%s' (%s)."),
		    zfs_get_name(zhp), prop_encroot);
		ret = EINVAL;
		goto error;
	}

	/* check that the key is loaded */
	keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
	if (keystatus == ZFS_KEYSTATUS_UNAVAILABLE) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Key already unloaded for '%s'."), zfs_get_name(zhp));
		ret = EACCES;
		goto error;
	}

	/* call the ioctl */
	ret = lzc_unload_key(zhp->zfs_name);

	if (ret != 0) {
		switch (ret) {
		case EPERM:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Permission denied."));
			break;
		case EACCES:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Key already unloaded for '%s'."),
			    zfs_get_name(zhp));
			break;
		case EBUSY:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "'%s' is busy."), zfs_get_name(zhp));
			break;
		}
		zfs_error(zhp->zfs_hdl, EZFS_CRYPTOFAILED, errbuf);
	}

	return (ret);

error:
	zfs_error(zhp->zfs_hdl, EZFS_CRYPTOFAILED, errbuf);
	return (ret);
}

static int
zfs_crypto_verify_rewrap_nvlist(zfs_handle_t *zhp, nvlist_t *props,
    nvlist_t **props_out, char *errbuf)
{
	int ret;
	nvpair_t *elem = NULL;
	zfs_prop_t prop;
	nvlist_t *new_props = NULL;

	new_props = fnvlist_alloc();

	/*
	 * loop through all provided properties, we should only have
	 * keyformat, keylocation and pbkdf2iters. The actual validation of
	 * values is done by zfs_valid_proplist().
	 */
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		const char *propname = nvpair_name(elem);
		prop = zfs_name_to_prop(propname);

		switch (prop) {
		case ZFS_PROP_PBKDF2_ITERS:
		case ZFS_PROP_KEYFORMAT:
		case ZFS_PROP_KEYLOCATION:
			break;
		default:
			ret = EINVAL;
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Only keyformat, keylocation and pbkdf2iters may "
			    "be set with this command."));
			goto error;
		}
	}

	new_props = zfs_valid_proplist(zhp->zfs_hdl, zhp->zfs_type, props,
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED), NULL, zhp->zpool_hdl,
	    B_TRUE, errbuf);
	if (new_props == NULL) {
		ret = EINVAL;
		goto error;
	}

	*props_out = new_props;
	return (0);

error:
	nvlist_free(new_props);
	*props_out = NULL;
	return (ret);
}

int
zfs_crypto_rewrap(zfs_handle_t *zhp, nvlist_t *raw_props, boolean_t inheritkey)
{
	int ret;
	char errbuf[ERRBUFLEN];
	boolean_t is_encroot;
	nvlist_t *props = NULL;
	uint8_t *wkeydata = NULL;
	uint_t wkeylen = 0;
	dcp_cmd_t cmd = (inheritkey) ? DCP_CMD_INHERIT : DCP_CMD_NEW_KEY;
	uint64_t crypt, pcrypt, keystatus, pkeystatus;
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	zfs_handle_t *pzhp = NULL;
	const char *keylocation = NULL;
	char origin_name[MAXNAMELEN];
	char prop_keylocation[MAXNAMELEN];
	char parent_name[ZFS_MAX_DATASET_NAME_LEN];

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "Key change error"));

	/* check that encryption is enabled for the pool */
	if (!encryption_feature_is_enabled(zhp->zpool_hdl)) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Encryption feature not enabled."));
		ret = EINVAL;
		goto error;
	}

	/* get crypt from dataset */
	crypt = zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION);
	if (crypt == ZIO_CRYPT_OFF) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Dataset not encrypted."));
		ret = EINVAL;
		goto error;
	}

	/* get the encryption root of the dataset */
	ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, NULL);
	if (ret != 0) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Failed to get encryption root for '%s'."),
		    zfs_get_name(zhp));
		goto error;
	}

	/* Clones use their origin's key and cannot rewrap it */
	ret = zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin_name,
	    sizeof (origin_name), NULL, NULL, 0, B_TRUE);
	if (ret == 0 && strcmp(origin_name, "") != 0) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Keys cannot be changed on clones."));
		ret = EINVAL;
		goto error;
	}

	/*
	 * If the user wants to use the inheritkey variant of this function
	 * we don't need to collect any crypto arguments.
	 */
	if (!inheritkey) {
		/* validate the provided properties */
		ret = zfs_crypto_verify_rewrap_nvlist(zhp, raw_props, &props,
		    errbuf);
		if (ret != 0)
			goto error;

		/*
		 * Load keyformat and keylocation from the nvlist. Fetch from
		 * the dataset properties if not specified.
		 */
		(void) nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), &keyformat);
		(void) nvlist_lookup_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &keylocation);

		if (is_encroot) {
			/*
			 * If this is already an encryption root, just keep
			 * any properties not set by the user.
			 */
			if (keyformat == ZFS_KEYFORMAT_NONE) {
				keyformat = zfs_prop_get_int(zhp,
				    ZFS_PROP_KEYFORMAT);
				ret = nvlist_add_uint64(props,
				    zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
				    keyformat);
				if (ret != 0) {
					zfs_error_aux(zhp->zfs_hdl,
					    dgettext(TEXT_DOMAIN, "Failed to "
					    "get existing keyformat "
					    "property."));
					goto error;
				}
			}

			if (keylocation == NULL) {
				ret = zfs_prop_get(zhp, ZFS_PROP_KEYLOCATION,
				    prop_keylocation, sizeof (prop_keylocation),
				    NULL, NULL, 0, B_TRUE);
				if (ret != 0) {
					zfs_error_aux(zhp->zfs_hdl,
					    dgettext(TEXT_DOMAIN, "Failed to "
					    "get existing keylocation "
					    "property."));
					goto error;
				}

				keylocation = prop_keylocation;
			}
		} else {
			/* need a new key for non-encryption roots */
			if (keyformat == ZFS_KEYFORMAT_NONE) {
				ret = EINVAL;
				zfs_error_aux(zhp->zfs_hdl,
				    dgettext(TEXT_DOMAIN, "Keyformat required "
				    "for new encryption root."));
				goto error;
			}

			/* default to prompt if no keylocation is specified */
			if (keylocation == NULL) {
				keylocation = "prompt";
				ret = nvlist_add_string(props,
				    zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
				    keylocation);
				if (ret != 0)
					goto error;
			}
		}

		/* fetch the new wrapping key and associated properties */
		ret = populate_create_encryption_params_nvlists(zhp->zfs_hdl,
		    zhp, B_TRUE, keyformat, keylocation, props, &wkeydata,
		    &wkeylen);
		if (ret != 0)
			goto error;
	} else {
		/* check that zhp is an encryption root */
		if (!is_encroot) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Key inheritting can only be performed on "
			    "encryption roots."));
			ret = EINVAL;
			goto error;
		}

		/* get the parent's name */
		ret = zfs_parent_name(zhp, parent_name, sizeof (parent_name));
		if (ret != 0) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Root dataset cannot inherit key."));
			ret = EINVAL;
			goto error;
		}

		/* get a handle to the parent */
		pzhp = make_dataset_handle(zhp->zfs_hdl, parent_name);
		if (pzhp == NULL) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Failed to lookup parent."));
			ret = ENOENT;
			goto error;
		}

		/* parent must be encrypted */
		pcrypt = zfs_prop_get_int(pzhp, ZFS_PROP_ENCRYPTION);
		if (pcrypt == ZIO_CRYPT_OFF) {
			zfs_error_aux(pzhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Parent must be encrypted."));
			ret = EINVAL;
			goto error;
		}

		/* check that the parent's key is loaded */
		pkeystatus = zfs_prop_get_int(pzhp, ZFS_PROP_KEYSTATUS);
		if (pkeystatus == ZFS_KEYSTATUS_UNAVAILABLE) {
			zfs_error_aux(pzhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Parent key must be loaded."));
			ret = EACCES;
			goto error;
		}
	}

	/* check that the key is loaded */
	keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
	if (keystatus == ZFS_KEYSTATUS_UNAVAILABLE) {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "Key must be loaded."));
		ret = EACCES;
		goto error;
	}

	/* call the ioctl */
	ret = lzc_change_key(zhp->zfs_name, cmd, props, wkeydata, wkeylen);
	if (ret != 0) {
		switch (ret) {
		case EPERM:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Permission denied."));
			break;
		case EINVAL:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Invalid properties for key change."));
			break;
		case EACCES:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "Key is not currently loaded."));
			break;
		}
		zfs_error(zhp->zfs_hdl, EZFS_CRYPTOFAILED, errbuf);
	}

	if (pzhp != NULL)
		zfs_close(pzhp);
	if (props != NULL)
		nvlist_free(props);
	if (wkeydata != NULL)
		free(wkeydata);

	return (ret);

error:
	if (pzhp != NULL)
		zfs_close(pzhp);
	if (props != NULL)
		nvlist_free(props);
	if (wkeydata != NULL)
		free(wkeydata);

	zfs_error(zhp->zfs_hdl, EZFS_CRYPTOFAILED, errbuf);
	return (ret);
}
