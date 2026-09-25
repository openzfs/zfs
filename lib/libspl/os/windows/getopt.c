/*	$NetBSD: getopt.c,v 1.10 1997/07/21 14:08:51 jtc Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#if HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "from: @(#)getopt.c	8.2 (Berkeley) 4/2/94";
#else
__RCSID("$NetBSD: getopt.c,v 1.10 1997/07/21 14:08:51 jtc Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>


#ifdef __weak_alias
__weak_alias(getopt, _getopt);
#endif


int	opterr = 1,		/* if error message should be printed */
    optind = 1,		/* index into parent argv vector */
    optopt,			/* character checked for validity */
    optreset;		/* reset getopt */
char *optarg;		/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

/*
 * getopt --
 *	Parse argc/argv argument vector.
 */
int
getopt(
    int nargc,
    char *const *nargv,
    const char *ostr)
{
	static char *place = EMSG;		/* option letter processing */
	char *oli;	/* option letter list index */

	if (optreset || !*place) {	/* update scanning pointer */
		optreset = 0;
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = EMSG;
			return (-1);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++optind;
			place = EMSG;
			return (-1);
		}
	}					/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
	    !(oli = strchr(ostr, optopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means -1.
		 */
		if (optopt == (int)'-')
			return (-1);
		if (!*place)
			++optind;
		if (opterr && *ostr != ':')
			(void) fprintf(stderr,
			    ": illegal option -- %c\n", optopt);
		return (BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	} else {					/* need an argument */
		if (*place)			/* no white space */
			optarg = place;
		else if (nargc <= ++optind) {	/* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (opterr)
				(void) fprintf(stderr,
				    ": option requires an argument -- %c\n",
				    optopt);
			return (BADCH);
		} else				/* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return (optopt);			/* dump back option letter */
}

int
getsubopt(
    char **optionsp,
    char *tokens[],
    char **valuep)
{
	register char *s = *optionsp, *p;
	register int i, optlen;

	*valuep = NULL;
	if (*s == '\0')
		return (-1);
	p = strchr(s, ',');		/* find next option */
	if (p == NULL) {
		p = s + strlen(s);
	} else {
		*p++ = '\0';	/* mark end and point to next */
	}
	*optionsp = p;		/* point to next option */
	p = strchr(s, '=');	/* find value */
	if (p == NULL) {
		optlen = strlen(s);
		*valuep = NULL;
	} else {
		optlen = p - s;
		*valuep = ++p;
	}
	for (i = 0; tokens[i] != NULL; i++) {
		if ((optlen == strlen(tokens[i])) &&
		    (strncmp(s, tokens[i], optlen) == 0))
			return (i);
	}
	/* no match, point value at option and return error */
	*valuep = s;
	return (-1);
}

static struct getopt_private_state {
	const char *optptr;
	const char *last_optstring;
	char *const *last_argv;
} pvt;

static inline const char *option_matches(const char *arg_str,
    const char *opt_name)
{
	while (*arg_str != '\0' && *arg_str != '=') {
		if (*arg_str++ != *opt_name++)
			return (NULL);
	}

	if (*opt_name)
		return (NULL);

	return (arg_str);
}

int
getopt_long(int argc, char *const *argv, const char *optstring,
    const struct option *longopts, int *longindex)
{
	const char *carg;
	const char *osptr;
	int opt;

	/*
	 * getopt() relies on a number of different global state
	 * variables, which can make this really confusing if there is
	 * more than one use of getopt() in the same program.  This
	 * attempts to detect that situation by detecting if the
	 * "optstring" or "argv" argument have changed since last time
	 * we were called; if so, reinitialize the query state.
	 */

	if (optstring != pvt.last_optstring || argv != pvt.last_argv ||
	    optind < 1 || optind > argc) {
		/* optind doesn't match the current query */
		pvt.last_optstring = optstring;
		pvt.last_argv = argv;
		optind = 1;
		pvt.optptr = NULL;
	}

	carg = argv[optind];

	/* First, eliminate all non-option cases */

	if (!carg || carg[0] != '-' || !carg[1])
		return (-1);

	if (carg[1] == '-') {
		const struct option *lo;
		const char *opt_end = NULL;

		optind++;

		/* Either it's a long option, or it's -- */
		if (!carg[2]) {
			/* It's -- */
			return (-1);
		}

		for (lo = longopts; lo->name; lo++) {
			if ((opt_end = option_matches(carg + 2, lo->name)))
				break;
		}
		if (!opt_end)
			return ('?');

		if (longindex)
			*longindex = lo - longopts;

		if (*opt_end == '=') {
			if (lo->has_arg)
				optarg = (char *)opt_end + 1;
			else
				return ('?');
		} else if (lo->has_arg == 1) {
			if (!(optarg = argv[optind]))
				return ('?');
			optind++;
		}

		if (lo->flag) {
			*lo->flag = lo->val;
			return (0);
		} else {
			return (lo->val);
		}
	}

	if ((uintptr_t)(pvt.optptr - carg) > (uintptr_t)strlen(carg)) {
		/* Someone frobbed optind, change to new opt. */
		pvt.optptr = carg + 1;
	}

	opt = *pvt.optptr++;

	if (opt != ':' && (osptr = strchr(optstring, opt))) {
		if (osptr[1] == ':') {
			if (*pvt.optptr) {
				/*
				 * Argument-taking option with attached
				 * argument
				 */
				optarg = (char *)pvt.optptr;
				optind++;
			} else {
				/*
				 * Argument-taking option with non-attached
				 * argument
				 */
				if (argv[optind + 1]) {
					optarg = (char *)argv[optind + 1];
					optind += 2;
				} else {
					/* Missing argument */
					optind++;
					return (optstring[0] == ':')
					    ? ':' : '?';
				}
			}
			return (opt);
		} else {
			/*
			 * Non-argument-taking option
			 * pvt.optptr will remember the exact position to
			 * resume at
			 */
			if (!*pvt.optptr)
				optind++;
			return (opt);
		}
	} else {
		/* Unknown option */
		optopt = opt;
		if (!*pvt.optptr)
			optind++;
		return ('?');
	}
}
