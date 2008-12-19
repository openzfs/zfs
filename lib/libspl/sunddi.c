/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdlib.h>
#include <errno.h>
#include <sys/sunddi.h>

int
ddi_strtoul(const char *str, char **endptr, int base,
	    unsigned long *result)
{
	unsigned long val;

	errno = 0;
	val = strtoul(str, endptr, base);
	if (errno == 0)
		*result = val;

	return errno;
}

int
ddi_strtol(const char *str, char **endptr, int base,
	   long *result)
{
	long val;

	errno = 0;
	val = strtol(str, endptr, base);
	if (errno == 0)
		*result = val;

	return errno;
}

int
ddi_strtoull(const char *str, char **endptr, int base,
	     unsigned long long *result)
{
	unsigned long long val;

	errno = 0;
	val = strtoull(str, endptr, base);
	if (errno == 0)
		*result = val;

	return errno;
}

int
ddi_strtoll(const char *str, char **endptr, int base,
	    long long *result)
{
	long long val;

	errno = 0;
	val = strtoll(str, endptr, base);
	if (errno == 0)
		*result = val;

	return errno;
}

/* FIXME: Unimplemented */
dev_info_t *
ddi_root_node(void)
{
	return NULL;
}

/* FIXME: Unimplemented */
int
ddi_prop_lookup_string(dev_t match_dev, dev_info_t *dip, uint_t flags,
                       char *name, char **data)
{
	*data = NULL;
	return ENOSYS;
}

/* FIXME: Unimplemented */
void
ddi_prop_free(void *datap)
{
	return;
}
