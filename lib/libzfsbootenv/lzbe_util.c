/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/*
 * Copyright 2020 Toomas Soome <tsoome@me.com>
 */

#include <sys/types.h>
#include <string.h>
#include <libzfs.h>
#include <libzfsbootenv.h>

/*
 * Output bootenv information.
 */
int
lzbe_bootenv_print(const char *pool, const char *nvlist, FILE *of)
{
	nvlist_t *nv;
	int rv = -1;

	if (pool == NULL || *pool == '\0' || of == NULL)
		return (rv);

	rv = lzbe_nvlist_get(pool, nvlist, (void **)&nv);
	if (rv == 0) {
		nvlist_print(of, nv);
		nvlist_free(nv);
	}

	return (rv);
}
