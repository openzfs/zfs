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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)envvar.c	1.5	05/06/08 SMI" */

#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include "umem_base.h"
#include "vmem_base.h"

/*
 * A umem environment variable, like UMEM_DEBUG, is set to a series
 * of items, seperated by ',':
 *
 *   UMEM_DEBUG="audit=10,guards,firewall=512"
 *
 * This structure describes items.  Each item has a name, type, and
 * description.  During processing, an item read from the user may
 * be either "valid" or "invalid".
 *
 * A valid item has an argument, if required, and it is of the right
 * form (doesn't overflow, doesn't contain any unexpected characters).
 *
 * If the item is valid, item_flag_target != NULL, and:
 *	type is not CLEARFLAG, then (*item_flag_target) |= item_flag_value
 *	type is CLEARFLAG, then (*item_flag_target) &= ~item_flag_value
 */

#define	UMEM_ENV_ITEM_MAX	512

struct umem_env_item;

typedef int arg_process_t(const struct umem_env_item *item, const char *value);
#define	ARG_SUCCESS	0	/* processing successful */
#define	ARG_BAD		1	/* argument had a bad value */

typedef struct umem_env_item {
	const char *item_name;	/* tag in environment variable */
	const char *item_interface_stability;
	enum {
	    ITEM_INVALID,
	    ITEM_FLAG,		/* only a flag.  No argument allowed */
	    ITEM_CLEARFLAG,	/* only a flag, but clear instead of set */
	    ITEM_OPTUINT,	/* optional integer argument */
	    ITEM_UINT,		/* required integer argument */
	    ITEM_OPTSIZE,	/* optional size_t argument */
	    ITEM_SIZE,		/* required size_t argument */
	    ITEM_SPECIAL	/* special argument processing */
	} item_type;
	const char *item_description;
	uint_t *item_flag_target; /* the variable containing the flag */
	uint_t item_flag_value;	/* the value to OR in */
	uint_t *item_uint_target; /* the variable to hold the integer */
	size_t *item_size_target;
	arg_process_t *item_special; /* callback for special handling */
} umem_env_item_t;

#ifndef UMEM_STANDALONE
static arg_process_t umem_backend_process;
#endif

static arg_process_t umem_log_process;

const char *____umem_environ_msg_options = "-- UMEM_OPTIONS --";

static umem_env_item_t umem_options_items[] = {
#ifndef UMEM_STANDALONE
	{ "backend",		"Evolving",	ITEM_SPECIAL,
		"=sbrk for sbrk(2), =mmap for mmap(2)",
		NULL, 0, NULL, NULL,
		&umem_backend_process
	},
#endif

	{ "concurrency",	"Private",	ITEM_UINT,
		"Max concurrency",
		NULL, 0,	&umem_max_ncpus
	},
	{ "max_contention",	"Private",	ITEM_UINT,
		"Maximum contention in a reap interval before the depot is "
		    "resized.",
		NULL, 0,	&umem_depot_contention
	},
	{ "nomagazines",	"Private",	ITEM_FLAG,
		"no caches will be multithreaded, and no caching will occur.",
		&umem_flags,	UMF_NOMAGAZINE
	},
	{ "reap_interval",	"Private",	ITEM_UINT,
		"Minimum time between reaps and updates, in seconds.",
		NULL, 0,	&umem_reap_interval
	},

#ifndef _WIN32
#ifndef UMEM_STANDALONE
	{ "sbrk_pagesize",	"Private",	ITEM_SIZE,
		"The preferred page size for the sbrk(2) heap.",
		NULL, 0, NULL,	&vmem_sbrk_pagesize
	},
#endif
#endif

	{ NULL, "-- end of UMEM_OPTIONS --",	ITEM_INVALID }
};

const char *____umem_environ_msg_debug = "-- UMEM_DEBUG --";

static umem_env_item_t umem_debug_items[] = {
	{ "default",		"Unstable",	ITEM_FLAG,
		"audit,contents,guards",
		&umem_flags,
		UMF_AUDIT | UMF_CONTENTS | UMF_DEADBEEF | UMF_REDZONE
	},
	{ "audit",		"Unstable",	ITEM_OPTUINT,
		"Enable auditing.  optionally =frames to set the number of "
		    "stored stack frames",
		&umem_flags,	UMF_AUDIT,	&umem_stack_depth
	},
	{ "contents",		"Unstable",	ITEM_OPTSIZE,
		"Enable contents storing.  UMEM_LOGGING=contents also "
		    "required.  optionally =bytes to set the number of stored "
		    "bytes",
		&umem_flags,	UMF_CONTENTS, NULL,	&umem_content_maxsave
	},
	{ "guards",		"Unstable",	ITEM_FLAG,
		"Enables guards and special patterns",
		&umem_flags,	UMF_DEADBEEF | UMF_REDZONE
	},
	{ "verbose",		"Unstable",	ITEM_FLAG,
		"Enables writing error messages to stderr",
		&umem_output,	1
	},

	{ "nosignal",	"Private",	ITEM_FLAG,
		"Abort if called from a signal handler.  Turns on 'audit'.  "
		    "Note that this is not always a bug.",
		&umem_flags,	UMF_AUDIT | UMF_CHECKSIGNAL
	},
	{ "firewall",		"Private",	ITEM_SIZE,
		"=minbytes.  Every object >= minbytes in size will have its "
		    "end against an unmapped page",
		&umem_flags,	UMF_FIREWALL,	NULL,	&umem_minfirewall
	},
	{ "lite",		"Private",	ITEM_FLAG,
		"debugging-lite",
		&umem_flags,	UMF_LITE
	},
	{ "maxverify",		"Private",	ITEM_SIZE,
		"=maxbytes, Maximum bytes to check when 'guards' is active. "
		    "Normally all bytes are checked.",
		NULL, 0, NULL,	&umem_maxverify
	},
	{ "noabort",		"Private",	ITEM_CLEARFLAG,
		"umem will not abort when a recoverable error occurs "
		    "(i.e. double frees, certain kinds of corruption)",
		&umem_abort,	1
	},
	{ "mtbf",		"Private",	ITEM_UINT,
		"=mtbf, the mean time between injected failures.  Works best "
		    "if prime.\n",
		NULL, 0,	&umem_mtbf
	},
	{ "random",		"Private",	ITEM_FLAG,
		"randomize flags on a per-cache basis",
		&umem_flags,	UMF_RANDOMIZE
	},
	{ "allverbose",		"Private",	ITEM_FLAG,
		"Enables writing all logged messages to stderr",
		&umem_output,	2
	},

	{ NULL, "-- end of UMEM_DEBUG --",	ITEM_INVALID }
};

const char *____umem_environ_msg_logging = "-- UMEM_LOGGING --";

static umem_env_item_t umem_logging_items[] = {
	{ "transaction",	"Unstable",	ITEM_SPECIAL,
		"If 'audit' is set in UMEM_DEBUG, the audit structures "
		    "from previous transactions are entered into this log.",
		NULL, 0, NULL,
		&umem_transaction_log_size,	&umem_log_process
	},
	{ "contents",		"Unstable",	ITEM_SPECIAL,
		"If 'audit' is set in UMEM_DEBUG, the contents of objects "
		    "are recorded in this log as they are freed.  If the "
		    "'contents' option is not set in UMEM_DEBUG, the first "
		    "256 bytes of each freed buffer will be saved.",
		&umem_flags,	UMF_CONTENTS,	NULL,
		&umem_content_log_size,		&umem_log_process
	},
	{ "fail",		"Unstable",	ITEM_SPECIAL,
		"Records are entered into this log for every failed "
		    "allocation.",
		NULL, 0, NULL,
		&umem_failure_log_size,		&umem_log_process
	},

	{ "slab",		"Private",	ITEM_SPECIAL,
		"Every slab created will be entered into this log.",
		NULL, 0, NULL,
		&umem_slab_log_size,		&umem_log_process
	},

	{ NULL, "-- end of UMEM_LOGGING --",	ITEM_INVALID }
};

typedef struct umem_envvar {
	const char *env_name;
	const char *env_func;
	umem_env_item_t	*env_item_list;
	const char *env_getenv_result;
	const char *env_func_result;
} umem_envvar_t;

static umem_envvar_t umem_envvars[] = {
	{ "UMEM_DEBUG",		"_umem_debug_init",	umem_debug_items },
	{ "UMEM_OPTIONS",	"_umem_options_init",	umem_options_items },
	{ "UMEM_LOGGING",	"_umem_logging_init",	umem_logging_items },
	{ NULL, NULL, NULL }
};

static umem_envvar_t *env_current;
#define	CURRENT		(env_current->env_name)

static int
empty(const char *str)
{
	char c;

	while ((c = *str) != '\0' && isspace(c))
		str++;

	return (*str == '\0');
}

static int
item_uint_process(const umem_env_item_t *item, const char *item_arg)
{
	ulong_t result;
	char *endptr = "";
	int olderrno;

	olderrno = errno;
	errno = 0;

	if (empty(item_arg)) {
		goto badnumber;
	}

	result = strtoul(item_arg, &endptr, 10);

	if (result == ULONG_MAX && errno == ERANGE) {
		errno = olderrno;
		goto overflow;
	}
	errno = olderrno;

	if (*endptr != '\0')
		goto badnumber;
	if ((uint_t)result != result)
		goto overflow;

	(*item->item_uint_target) = (uint_t)result;
	return (ARG_SUCCESS);

badnumber:
	log_message("%s: %s: not a number\n", CURRENT, item->item_name);
	return (ARG_BAD);

overflow:
	log_message("%s: %s: overflowed\n", CURRENT, item->item_name);
	return (ARG_BAD);
}

static int
item_size_process(const umem_env_item_t *item, const char *item_arg)
{
	ulong_t result;
	ulong_t result_arg;
	char *endptr = "";
	int olderrno;

	if (empty(item_arg))
		goto badnumber;

	olderrno = errno;
	errno = 0;

	result_arg = strtoul(item_arg, &endptr, 10);

	if (result_arg == ULONG_MAX && errno == ERANGE) {
		errno = olderrno;
		goto overflow;
	}
	errno = olderrno;

	result = result_arg;

	switch (*endptr) {
	case 't':
	case 'T':
		result *= 1024;
		if (result < result_arg)
			goto overflow;
		/*FALLTHRU*/
	case 'g':
	case 'G':
		result *= 1024;
		if (result < result_arg)
			goto overflow;
		/*FALLTHRU*/
	case 'm':
	case 'M':
		result *= 1024;
		if (result < result_arg)
			goto overflow;
		/*FALLTHRU*/
	case 'k':
	case 'K':
		result *= 1024;
		if (result < result_arg)
			goto overflow;
		endptr++;		/* skip over the size character */
		break;
	default:
		break;			/* handled later */
	}

	if (*endptr != '\0')
		goto badnumber;

	(*item->item_size_target) = result;
	return (ARG_SUCCESS);

badnumber:
	log_message("%s: %s: not a number\n", CURRENT, item->item_name);
	return (ARG_BAD);

overflow:
	log_message("%s: %s: overflowed\n", CURRENT, item->item_name);
	return (ARG_BAD);
}

static int
umem_log_process(const umem_env_item_t *item, const char *item_arg)
{
	if (item_arg != NULL) {
		int ret;
		ret = item_size_process(item, item_arg);
		if (ret != ARG_SUCCESS)
			return (ret);

		if (*item->item_size_target == 0)
			return (ARG_SUCCESS);
	} else
		*item->item_size_target = 64*1024;

	umem_logging = 1;
	return (ARG_SUCCESS);
}

#ifndef UMEM_STANDALONE
static int
umem_backend_process(const umem_env_item_t *item, const char *item_arg)
{
	const char *name = item->item_name;

	if (item_arg == NULL)
		goto fail;

	if (strcmp(item_arg, "sbrk") == 0)
		vmem_backend |= VMEM_BACKEND_SBRK;
	else if (strcmp(item_arg, "mmap") == 0)
		vmem_backend |= VMEM_BACKEND_MMAP;
	else
		goto fail;

	return (ARG_SUCCESS);

fail:
	log_message("%s: %s: must be %s=sbrk or %s=mmap\n",
	    CURRENT, name, name, name);
	return (ARG_BAD);
}
#endif

static int
process_item(const umem_env_item_t *item, const char *item_arg)
{
	int arg_required = 0;
	arg_process_t *processor;

	switch (item->item_type) {
	case ITEM_FLAG:
	case ITEM_CLEARFLAG:
	case ITEM_OPTUINT:
	case ITEM_OPTSIZE:
	case ITEM_SPECIAL:
		arg_required = 0;
		break;

	case ITEM_UINT:
	case ITEM_SIZE:
		arg_required = 1;
		break;

	default:
		log_message("%s: %s: Invalid type.  Ignored\n",
		    CURRENT, item->item_name);
		return (1);
	}

	switch (item->item_type) {
	case ITEM_FLAG:
	case ITEM_CLEARFLAG:
		if (item_arg != NULL) {
			log_message("%s: %s: does not take a value. ignored\n",
			    CURRENT, item->item_name);
			return (1);
		}
		processor = NULL;
		break;

	case ITEM_UINT:
	case ITEM_OPTUINT:
		processor = item_uint_process;
		break;

	case ITEM_SIZE:
	case ITEM_OPTSIZE:
		processor = item_size_process;
		break;

	case ITEM_SPECIAL:
		processor = item->item_special;
		break;

	default:
		log_message("%s: %s: Invalid type.  Ignored\n",
		    CURRENT, item->item_name);
		return (1);
	}

	if (arg_required && item_arg == NULL) {
		log_message("%s: %s: Required value missing\n",
		    CURRENT, item->item_name);
		goto invalid;
	}

	if (item_arg != NULL || item->item_type == ITEM_SPECIAL) {
		if (processor(item, item_arg) != ARG_SUCCESS)
			goto invalid;
	}

	if (item->item_flag_target) {
		if (item->item_type == ITEM_CLEARFLAG)
			(*item->item_flag_target) &= ~item->item_flag_value;
		else
			(*item->item_flag_target) |= item->item_flag_value;
	}
	return (0);

invalid:
	return (1);
}

#define	ENV_SHORT_BYTES	10	/* bytes to print on error */
void
umem_process_value(umem_env_item_t *item_list, const char *beg, const char *end)
{
	char buf[UMEM_ENV_ITEM_MAX];
	char *argptr;

	size_t count;

	while (beg < end && isspace(*beg))
		beg++;

	while (beg < end && isspace(*(end - 1)))
		end--;

	if (beg >= end) {
		log_message("%s: empty option\n", CURRENT);
		return;
	}

	count = end - beg;

	if (count + 1 > sizeof (buf)) {
		char outbuf[ENV_SHORT_BYTES + 1];
		/*
		 * Have to do this, since sprintf("%10s",...) calls malloc()
		 */
		(void) strncpy(outbuf, beg, ENV_SHORT_BYTES);
		outbuf[ENV_SHORT_BYTES] = 0;

		log_message("%s: argument \"%s...\" too long\n", CURRENT,
		    outbuf);
		return;
	}

	(void) strncpy(buf, beg, count);
	buf[count] = 0;

	argptr = strchr(buf, '=');

	if (argptr != NULL)
		*argptr++ = 0;

	for (; item_list->item_name != NULL; item_list++) {
		if (strcmp(buf, item_list->item_name) == 0) {
			(void) process_item(item_list, argptr);
			return;
		}
	}
	log_message("%s: '%s' not recognized\n", CURRENT, buf);
}

/*ARGSUSED*/
void
umem_setup_envvars(int invalid)
{
	umem_envvar_t *cur_env;
	static volatile enum {
		STATE_START,
		STATE_GETENV,
		STATE_DLSYM,
		STATE_FUNC,
		STATE_DONE
	} state = STATE_START;
#ifndef UMEM_STANDALONE
	void *h;
#endif

	if (invalid) {
		const char *where;
		/*
		 * One of the calls below invoked malloc() recursively.  We
		 * remove any partial results and return.
		 */

		switch (state) {
		case STATE_START:
			where = "before getenv(3C) calls -- "
			    "getenv(3C) results ignored.";
			break;
		case STATE_GETENV:
			where = "during getenv(3C) calls -- "
			    "getenv(3C) results ignored.";
			break;
		case STATE_DLSYM:
			where = "during dlsym(3C) call -- "
			    "_umem_*() results ignored.";
			break;
		case STATE_FUNC:
			where = "during _umem_*() call -- "
			    "_umem_*() results ignored.";
			break;
		case STATE_DONE:
			where = "after dlsym() or _umem_*() calls.";
			break;
		default:
			where = "at unknown point -- "
			    "_umem_*() results ignored.";
			break;
		}

		log_message("recursive allocation %s\n", where);

		for (cur_env = umem_envvars; cur_env->env_name != NULL;
		    cur_env++) {
			if (state == STATE_GETENV)
				cur_env->env_getenv_result = NULL;
			if (state != STATE_DONE)
				cur_env->env_func_result = NULL;
		}

		state = STATE_DONE;
		return;
	}

	state = STATE_GETENV;

	for (cur_env = umem_envvars; cur_env->env_name != NULL; cur_env++) {
		cur_env->env_getenv_result = getenv(cur_env->env_name);
		if (state == STATE_DONE)
			return;		/* recursed */
	}

#ifndef UMEM_STANDALONE
#ifdef _WIN32
# define dlopen(a, b)	GetModuleHandle(NULL)
# define dlsym(a, b)	GetProcAddress((HANDLE)a, b)
# define dlclose(a)		0
# define dlerror()		0
#endif
	/* get a handle to the "a.out" object */
	if ((h = dlopen(0, RTLD_FIRST | RTLD_LAZY)) != NULL) {
		for (cur_env = umem_envvars; cur_env->env_name != NULL;
		    cur_env++) {
			const char *(*func)(void);
			const char *value;

			state = STATE_DLSYM;
			func = (const char *(*)(void))dlsym(h,
			    cur_env->env_func);

			if (state == STATE_DONE)
				break;		/* recursed */

			state = STATE_FUNC;
			if (func != NULL) {
				value = func();
				if (state == STATE_DONE)
					break;		/* recursed */
				cur_env->env_func_result = value;
			}
		}
		(void) dlclose(h);
	} else {
		(void) dlerror();		/* snarf dlerror() */
	}
#endif /* UMEM_STANDALONE */

	state = STATE_DONE;
}

/*
 * Process the environment variables.
 */
void
umem_process_envvars(void)
{
	const char *value;
	const char *end, *next;
	umem_envvar_t *cur_env;

	for (cur_env = umem_envvars; cur_env->env_name != NULL; cur_env++) {
		env_current = cur_env;

		value = cur_env->env_getenv_result;
		if (value == NULL)
			value = cur_env->env_func_result;

		/* ignore if missing or empty */
		if (value == NULL)
			continue;

		for (end = value; *end != '\0'; value = next) {
			end = strchr(value, ',');
			if (end != NULL)
				next = end + 1;		/* skip the comma */
			else
				next = end = value + strlen(value);

			umem_process_value(cur_env->env_item_list, value, end);
		}
	}
}
