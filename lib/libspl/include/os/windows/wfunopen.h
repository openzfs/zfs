/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Windows wrapper to handle Unix
 * FILE *
 *  funopen(const void *cookie,
 *   int (*readfn)(void *, char *, int),
 *   int (*writefn)(void *, const char *, int),
 *   fpos_t (*seekfn)(void *, fpos_t, int),
 *   int (*closefn)(void *));
 *
 * An open function which returns a "FILE *", and takes callback functions
 * for read, write, seek, and close. A "cookie", or "void *private" pointer
 * is passed along to all the callbacks.
 *
 * Then any call to ordinary "FILE *" functions, like that of
 * fwrite, fread, getline, fprintf, and fclose (and so on) will
 * detect that the "FILE *" was funopen()ed and will instead call
 * the callbacks. Ie, a call to fwrite() will be changed to writefn().
 *
 * For the same of this project, we need relatively few interceptions.
 *
 * Copyright (c) 2024 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _LIBSPL_WFUNOPEN_H
#define	_LIBSPL_WFUNOPEN_H

#include <sys/errno.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	WFUNOPEN_MAGIC 0x5A46534D454D465AULL /* "ZFSMEMFZ" for fun */

struct fakeFILE_s {
	uint64_t magic;
	void *cookie;
	int (*readfn)(void *, char *, int);
	int (*writefn)(void *, const char *, int);
	fpos_t(*seekfn)(void *, fpos_t, int);
	int (*closefn)(void *);
	FILE *realFILE; /* If set, call normally */
};

typedef struct fakeFILE_s fakeFILE;

static inline int wosix_is_fake_file(FILE *f)
{
	if (!f)
		return (0);

	const fakeFILE *ff = (const fakeFILE *)f;
	return (ff->magic == WFUNOPEN_MAGIC);
}


static inline FILE *
wosix_funopen(void *cookie,
    int (*readfn)(void *, char *, int),
    int (*writefn)(void *, const char *, int),
    fpos_t(*seekfn)(void *, fpos_t, int),
    int (*closefn)(void *))
{
	fakeFILE *fFILE = (fakeFILE *)malloc(sizeof (fakeFILE));
	if (fFILE) {
		fFILE->magic = WFUNOPEN_MAGIC;
		fFILE->cookie = cookie;
		fFILE->seekfn = seekfn;
		fFILE->readfn = readfn;
		fFILE->writefn = writefn;
		fFILE->closefn = closefn;
		fFILE->realFILE = NULL;
	}

	return ((FILE *)fFILE);
}
#define	funopen wosix_funopen

#if 0
static inline FILE *wosix_fopenX(const char *path, const char *mode)
{
	FILE *realFILE;

	realFILE = fopen(path, mode);
	if (!realFILE)
		return (NULL);

	fakeFILE *fFILE = malloc(sizeof (fakeFILE));
	if (!fFILE) {
		fclose(realFILE);
		return (NULL);
	}

	fFILE->realFILE = realFILE;
	return ((FILE *)fFILE);
}
#define	fopen wosix_fopen
#endif
#define	fopen wosix_fopen

static inline int wosix_fclose(FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		return (fclose(f));

	if (fFILE->realFILE)
		result = fclose(fFILE->realFILE);
	else
		result = fFILE->closefn(fFILE->cookie);

	free(fFILE);
	return (result);
}
#define	fclose wosix_fclose

extern ssize_t getline_impl(char **linep, size_t *linecapp, FILE *stream,
    boolean_t internal);
extern ssize_t getline(char **linep, size_t *linecapp, FILE *stream);

static inline ssize_t wosix_getline(char **linep, size_t *linecap, FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		result = getline_impl(linep, linecap, f, (boolean_t)FALSE);
	else if (fFILE->realFILE)
		result = getline(linep, linecap, fFILE->realFILE);
	else
		result = getline_impl(linep, linecap, (FILE *)fFILE,
		    (boolean_t)TRUE);

	return (result);
}
#define	getline wosix_getline

static inline size_t wosix_fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		result = fread(ptr, size, nmemb, f);
	else if (fFILE->realFILE)
		result = fread(ptr, size, nmemb, fFILE->realFILE);
	else
		result = fFILE->readfn(fFILE->cookie, (char *)ptr,
		    size * nmemb);

	return (result);
}
#define	fread wosix_fread

static inline size_t wosix_fwrite(void *ptr, size_t size, size_t nmemb, FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		result = fwrite(ptr, size, nmemb, f);
	else if (fFILE->realFILE)
		result = fwrite(ptr, size, nmemb, fFILE->realFILE);
	else
		result = fFILE->writefn(fFILE->cookie, (const char *)ptr,
		    size * nmemb);

	return (result);
}
#define	fwrite wosix_fwrite

static inline int wosix_ferror(FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		result = ferror(f);
	else if (fFILE->realFILE)
		result = ferror(fFILE->realFILE);
	else
		result = 0;

	return (result);
}
#define	ferror wosix_ferror

static inline int wosix_fflush(FILE *f)
{
	fakeFILE *fFILE = (fakeFILE *)f;
	int result;

	if (!wosix_is_fake_file(f))
		result = fflush(f);
	else if (fFILE->realFILE)
		result = fflush(fFILE->realFILE);
	else
		result = 0;

	return (result);
}
#define	fflush wosix_fflush

static inline int wosix_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	fakeFILE *ff = (fakeFILE *)f;

	if (!wosix_is_fake_file(f))
		return (vfprintf(f, fmt, ap));

	if (ff->realFILE)
		return (vfprintf(ff->realFILE, fmt, ap));

	// MSVC/clang-cl: _vscprintf computes required chars (excluding '\0')
	va_list ap2;
	va_copy(ap2, ap);
	int need = _vscprintf(fmt, ap2);
	va_end(ap2);
	if (need < 0)
		return (-1);

	// +1 for NUL (vsnprintf wants it)
	char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)need + 1);
	if (!buf)
		return (-1);

	int n = vsnprintf(buf, (size_t)need + 1, fmt, ap);
	if (n < 0) {
		HeapFree(GetProcessHeap(), 0, buf);
		return (-1);
	}

	int rc = ff->writefn(ff->cookie, buf, n);
	HeapFree(GetProcessHeap(), 0, buf);
	return ((rc == n) ? n : -1);
}
#define	vfprintf wosix_vfprintf

static inline int wosix_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = wosix_vfprintf(f, fmt, ap);
	va_end(ap);
	return (r);
}

// We don't wrap the call here, as it affects far too much,
// better a consumer of this header file defines it when needed.
#define	fprintf wosix_fprintf


#ifdef  __cplusplus
}
#endif

#endif
