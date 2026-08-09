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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Creates directory and it's parents if the parents do not
 * exist yet.
 *
 * Returns -1 if fails for reasons other than non-existing
 * parents.
 * Does NOT simplify pathnames with . or .. in them.
 */

#include <sys/types.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static char *simplify(const char *str);

int
mkdirp(const char *d, mode_t mode)
{
	char  *endptr, *ptr, *slash, *str;

	str = simplify(d);

	/* If space couldn't be allocated for the simplified names, return. */

	if (str == NULL)
		return (-1);

		/* Try to make the directory */

	if (mkdir(str, mode) == 0) {
		free(str);
		return (0);
	}
	if (errno != ENOENT) {
		free(str);
		return (-1);
	}
	endptr = strrchr(str, '\0');
	slash = strrchr(str, '/');

		/* Search upward for the non-existing parent */

	while (slash != NULL) {

		ptr = slash;
		*ptr = '\0';

			/* If reached an existing parent, break */

		if (access(str, F_OK) == 0)
			break;

			/* If non-existing parent */

		else {
			slash = strrchr(str, '/');

				/* If under / or current directory, make it. */

			if (slash == NULL || slash == str) {
				if (mkdir(str, mode) != 0 && errno != EEXIST) {
					free(str);
					return (-1);
				}
				break;
			}
		}
	}

	/* Create directories starting from upmost non-existing parent */

	while ((ptr = strchr(str, '\0')) != endptr) {
		*ptr = '/';
		if (mkdir(str, mode) != 0 && errno != EEXIST) {
			/*
			 *  If the mkdir fails because str already
			 *  exists (EEXIST), then str has the form
			 *  "existing-dir/..", and this is really
			 *  ok. (Remember, this loop is creating the
			 *  portion of the path that didn't exist)
			 */
			free(str);
			return (-1);
		}
	}
	free(str);
	return (0);
}

/*
 *	simplify - given a pathname, simplify that path by removing
 *		   duplicate contiguous slashes.
 *
 *		   A simplified copy of the argument is returned to the
 *		   caller, or NULL is returned on error.
 *
 *		   The caller should handle error reporting based upon the
 *		   returned value, and should free the returned value,
 *		   when appropriate.
 */

static char *
simplify(const char *str)
{
	int i;
	size_t mbPathlen;	/* length of multi-byte path */
	size_t wcPathlen;	/* length of wide-character path */
	wchar_t *wptr;		/* scratch pointer */
	wchar_t *wcPath;	/* wide-character version of the path */
	char *mbPath;		/* The copy fo the path to be returned */

	/*
	 *  bail out if there is nothing there.
	 */

	if (!str) {
		errno = ENOENT;
		return (NULL);
	}

	/*
	 *  Get a copy of the argument.
	 */

	if ((mbPath = strdup(str)) == NULL) {
		return (NULL);
	}

	/*
	 *  convert the multi-byte version of the path to a
	 *  wide-character rendering, for doing our figuring.
	 */

	mbPathlen = strlen(mbPath);

	if ((wcPath = calloc(mbPathlen+1, sizeof (wchar_t))) == NULL) {
		free(mbPath);
		return (NULL);
	}

	if ((wcPathlen = mbstowcs(wcPath, mbPath, mbPathlen)) == (size_t)-1) {
		free(mbPath);
		free(wcPath);
		return (NULL);
	}

	/*
	 *  remove duplicate slashes first ("//../" -> "/")
	 */

	for (wptr = wcPath, i = 0; i < wcPathlen; i++) {
		*wptr++ = wcPath[i];

		if (wcPath[i] == '/') {
			i++;

			while (wcPath[i] == '/') {
				i++;
			}

			i--;
		}
	}

	*wptr = '\0';

	/*
	 *  now convert back to the multi-byte format.
	 */

	if (wcstombs(mbPath, wcPath, mbPathlen) == (size_t)-1) {
		free(mbPath);
		free(wcPath);
		return (NULL);
	}

	free(wcPath);
	return (mbPath);
}
