/*
 * Copyright © 2013 Guillem Jover <guillem@hadrons.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <libzutil.h>

static struct {
	/* Original value. */
	const char *arg0;

	/* Title space available. */
	char *base, *end;

	/* Pointer to original nul character within base. */
	char *nul;

	boolean_t warned;
	boolean_t reset;
	int error;
} SPT;

#define	LIBBSD_IS_PATHNAME_SEPARATOR(c) ((c) == '/')
#define	SPT_MAXTITLE 255

extern const char *__progname;

static const char *
getprogname(void)
{
	return (__progname);
}

static void
setprogname(const char *progname)
{
	size_t i;

	for (i = strlen(progname); i > 0; i--) {
		if (LIBBSD_IS_PATHNAME_SEPARATOR(progname[i - 1])) {
			__progname = progname + i;
			return;
		}
	}
	__progname = progname;
}


static inline size_t
spt_min(size_t a, size_t b)
{
	return ((a < b) ? a : b);
}

static int
spt_copyenv(int envc, char *envp[])
{
	char **envcopy;
	char *eq;
	int envsize;
	int i, error;

	if (environ != envp)
		return (0);

	/*
	 * Make a copy of the old environ array of pointers, in case
	 * clearenv() or setenv() is implemented to free the internal
	 * environ array, because we will need to access the old environ
	 * contents to make the new copy.
	 */
	envsize = (envc + 1) * sizeof (char *);
	envcopy = malloc(envsize);
	if (envcopy == NULL)
		return (errno);
	memcpy(envcopy, envp, envsize);

	environ = NULL;

	for (i = 0; envcopy[i]; i++) {
		eq = strchr(envcopy[i], '=');
		if (eq == NULL)
			continue;

		*eq = '\0';
		if (setenv(envcopy[i], eq + 1, 1) < 0)
			error = errno;
		*eq = '=';

		if (error) {
			clearenv();
			environ = envp;
			free(envcopy);
			return (error);
		}
	}

	/*
	 * Dispose of the shallow copy, now that we've finished transfering
	 * the old environment.
	 */
	free(envcopy);

	return (0);
}

static int
spt_copyargs(int argc, char *argv[])
{
	char *tmp;
	int i;

	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (argv[i] == NULL)
			continue;

		tmp = strdup(argv[i]);
		if (tmp == NULL)
			return (errno);

		argv[i] = tmp;
	}

	return (0);
}

void
zfs_setproctitle_init(int argc, char *argv[], char *envp[])
{
	char *base, *end, *nul, *tmp;
	int i, envc, error;

	/* Try to make sure we got called with main() arguments. */
	if (argc < 0)
		return;

	base = argv[0];
	if (base == NULL)
		return;

	nul = base + strlen(base);
	end = nul + 1;

	for (i = 0; i < argc || (i >= argc && argv[i]); i++) {
		if (argv[i] == NULL || argv[i] != end)
			continue;

		end = argv[i] + strlen(argv[i]) + 1;
	}

	for (i = 0; envp[i]; i++) {
		if (envp[i] != end)
			continue;

		end = envp[i] + strlen(envp[i]) + 1;
	}
	envc = i;

	SPT.arg0 = strdup(argv[0]);
	if (SPT.arg0 == NULL) {
		SPT.error = errno;
		return;
	}

	tmp = strdup(getprogname());
	if (tmp == NULL) {
		SPT.error = errno;
		return;
	}
	setprogname(tmp);

	error = spt_copyenv(envc, envp);
	if (error) {
		SPT.error = error;
		return;
	}

	error = spt_copyargs(argc, argv);
	if (error) {
		SPT.error = error;
		return;
	}

	SPT.nul  = nul;
	SPT.base = base;
	SPT.end  = end;
}

void
zfs_setproctitle(const char *fmt, ...)
{
	/* Use buffer in case argv[0] is passed. */
	char buf[SPT_MAXTITLE + 1];
	va_list ap;
	char *nul;
	int len;
	if (SPT.base == NULL) {
		if (!SPT.warned) {
			warnx("setproctitle not initialized, please"
			    "call zfs_setproctitle_init()");
			SPT.warned = B_TRUE;
		}
		return;
	}

	if (fmt) {
		if (fmt[0] == '-') {
			/* Skip program name prefix. */
			fmt++;
			len = 0;
		} else {
			/* Print program name heading for grep. */
			snprintf(buf, sizeof (buf), "%s: ", getprogname());
			len = strlen(buf);
		}

		va_start(ap, fmt);
		len += vsnprintf(buf + len, sizeof (buf) - len, fmt, ap);
		va_end(ap);
	} else {
		len = snprintf(buf, sizeof (buf), "%s", SPT.arg0);
	}

	if (len <= 0) {
		SPT.error = errno;
		return;
	}

	if (!SPT.reset) {
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = B_TRUE;
	} else {
		memset(SPT.base, 0, spt_min(sizeof (buf), SPT.end - SPT.base));
	}

	len = spt_min(len, spt_min(sizeof (buf), SPT.end - SPT.base) - 1);
	memcpy(SPT.base, buf, len);
	nul = SPT.base + len;

	if (nul < SPT.nul) {
		*SPT.nul = '.';
	} else if (nul == SPT.nul && nul + 1 < SPT.end) {
		*SPT.nul = ' ';
		*++nul = '\0';
	}
}
