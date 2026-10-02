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

#ifndef LIBSPL_GETOPT_H_INCLUDED
#define	LIBSPL_GETOPT_H_INCLUDED

#define	no_argument		0
#define	required_argument	1
#define	optional_argument	2

struct option
{
	const char *name;
	int has_arg;
	int *flag;
	int val;
};

extern int getopt(int, char * const *, const char *);
extern int getopt_long(int, char * const *, const char *,
    const struct option *, int *);
extern int getopt_long_only(int, char * const *, const char *,
    const struct option *, int *);
extern int getsubopt(char **optionsp, char *tokens[], char **valuep);

#endif // LIBSPL_GETOPT_H_INCLUDED
