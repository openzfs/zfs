// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/tunables.h>

/*
 * Userspace tunables.
 *
 * Tunables are external pointers to global variables that are wired up to the
 * host environment in some way that allows the operator to directly change
 * their values "under the hood".
 *
 * In userspace, the "host environment" is the program using libzpool.so. So
 * that it can manipulate tunables if it wants, we provide an API to access
 * them.
 *
 * Tunables are declared through the ZFS_MODULE_PARAM* macros, which associate
 * a global variable with some metadata we can use to describe and access the
 * tunable. This is done by creating a uniquely-named zfs_tunable_t.
 *
 * At runtime, we need a way to discover these zfs_tunable_t items. Since they
 * are declared globally, all over the codebase, there's no central place to
 * record or list them. So, we take advantage of the compiler's "linker set"
 * feature.
 *
 * In the ZFS_MODULE_PARAM macro, after we create the zfs_tunable_t, we also
 * create a zfs_tunable_t* pointing to it. That pointer is forced into the
 * "zfs_tunables" ELF section in compiled object. At link time, the linker will
 * collect all these pointers into one single big "zfs_tunable" section, and
 * will generate two new symbols in the final object: __start_zfs_tunable and
 * __stop_zfs_tunable. These point to the first and last item in that section,
 * which allows us to access the pointers in that section like an array, and
 * through those pointers access the tunable metadata, and from there the
 * actual C variable that the tunable describes.
 */

extern const zfs_tunable_t *__start_zfs_tunables;
extern const zfs_tunable_t *__stop_zfs_tunables;

/*
 * Because there are no tunables in libspl itself, the above symbols will not
 * be generated, which will stop libspl being linked at all. To work around
 * that, we force a symbol into that section, and then when iterating, skip
 * any NULL pointers.
 */
static void *__zfs_tunable__placeholder
	__attribute__((__section__("zfs_tunables")))
	__attribute__((__used__)) = NULL;

/*
 * Find the name tunable by walking through the linker set and comparing names,
 * as described above. This is not particularly efficient but it's a fairly
 * rare task, so it shouldn't be a big deal.
 */
const zfs_tunable_t *
zfs_tunable_lookup(const char *name)
{
	for (const zfs_tunable_t **ztp = &__start_zfs_tunables;
	    ztp != &__stop_zfs_tunables; ztp++) {
		const zfs_tunable_t *zt = *ztp;
		if (zt == NULL)
			continue;
		if (strcmp(name, zt->zt_name) == 0)
			return (zt);
	}

	return (NULL);
}

/*
 * Like zfs_tunable_lookup, but call the provided callback for each tunable.
 */
void
zfs_tunable_iter(zfs_tunable_iter_t cb, void *arg)
{
	for (const zfs_tunable_t **ztp = &__start_zfs_tunables;
	    ztp != &__stop_zfs_tunables; ztp++) {
		const zfs_tunable_t *zt = *ztp;
		if (zt == NULL)
			continue;
		if (cb(zt, arg))
			return;
	}
}

/*
 * Parse a string into an int or uint. It's easier to have a pair of "generic"
 * functions that clamp to a given min and max rather than have multiple
 * functions for each width of type.
 */
static int
zfs_tunable_parse_int(const char *val, intmax_t *np,
    intmax_t min, intmax_t max)
{
	intmax_t n;
	char *end;
	int err;

	errno = 0;
	n = strtoimax(val, &end, 0);
	if ((err = errno) != 0)
		return (err);
	if (*end != '\0')
		return (EINVAL);
	if (n < min || n > max)
		return (ERANGE);
	*np = n;
	return (0);
}

static int
zfs_tunable_parse_uint(const char *val, uintmax_t *np,
    uintmax_t min, uintmax_t max)
{
	uintmax_t n;
	char *end;
	int err;

	errno = 0;
	n = strtoumax(val, &end, 0);
	if ((err = errno) != 0)
		return (err);
	if (*end != '\0')
		return (EINVAL);
	if (strchr(val, '-'))
		return (ERANGE);
	if (n < min || n > max)
		return (ERANGE);
	*np = n;
	return (0);
}

/*
 * Set helpers for each tunable type. Parses the string, and if produces a
 * valid value for the tunable, sets it. No effort is made to make sure the
 * tunable is of the right type; that's done in zfs_tunable_set() below.
 */
static int
zfs_tunable_set_int(const zfs_tunable_t *zt, const char *val)
{
	intmax_t n;
	int err = zfs_tunable_parse_int(val, &n, INT_MIN, INT_MAX);
	if (err != 0)
		return (err);
	*(int *)zt->zt_varp = n;
	return (0);
}

static int
zfs_tunable_set_uint(const zfs_tunable_t *zt, const char *val)
{
	uintmax_t n;
	int err = zfs_tunable_parse_uint(val, &n, 0, UINT_MAX);
	if (err != 0)
		return (err);
	*(unsigned int *)zt->zt_varp = n;
	return (0);
}

static int
zfs_tunable_set_ulong(const zfs_tunable_t *zt, const char *val)
{
	uintmax_t n;
	int err = zfs_tunable_parse_uint(val, &n, 0, ULONG_MAX);
	if (err != 0)
		return (err);
	*(unsigned long *)zt->zt_varp = n;
	return (0);
}

static int
zfs_tunable_set_u64(const zfs_tunable_t *zt, const char *val)
{
	uintmax_t n;
	int err = zfs_tunable_parse_uint(val, &n, 0, UINT64_MAX);
	if (err != 0)
		return (err);
	*(uint64_t *)zt->zt_varp = n;
	return (0);
}

static int
zfs_tunable_set_string(const zfs_tunable_t *zt, const char *val)
{
	(void) zt, (void) val;
	/*
	 * We can't currently handle strings. String tunables are pointers
	 * into read-only memory, so we can update the pointer, but not the
	 * contents. That would mean taking an allocation, but we don't have
	 * an obvious place to free it.
	 *
	 * For now, it's no big deal as there's only a couple of string
	 * tunables anyway.
	 */
	return (ENOTSUP);
}

/*
 * Get helpers for each tunable type. Converts the value to a string if
 * necessary and writes it into the provided buffer. The type is assumed to
 * be correct; zfs_tunable_get() below will call the correct function for the
 * type.
 */
static int
zfs_tunable_get_int(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	snprintf(val, valsz, "%d", *(int *)zt->zt_varp);
	return (0);
}

static int
zfs_tunable_get_uint(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	snprintf(val, valsz, "%u", *(unsigned int *)zt->zt_varp);
	return (0);
}

static int
zfs_tunable_get_ulong(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	snprintf(val, valsz, "%lu", *(unsigned long *)zt->zt_varp);
	return (0);
}

static int
zfs_tunable_get_u64(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	snprintf(val, valsz, "%"PRIu64, *(uint64_t *)zt->zt_varp);
	return (0);
}

static int
zfs_tunable_get_string(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	strlcpy(val, *(char **)zt->zt_varp, valsz);
	return (0);
}

/* The public set function. Delegates to the type-specific version. */
int
zfs_tunable_set(const zfs_tunable_t *zt, const char *val)
{
	int err;
	switch (zt->zt_type) {
	case ZFS_TUNABLE_TYPE_INT:
		err = zfs_tunable_set_int(zt, val);
		break;
	case ZFS_TUNABLE_TYPE_UINT:
		err = zfs_tunable_set_uint(zt, val);
		break;
	case ZFS_TUNABLE_TYPE_ULONG:
		err = zfs_tunable_set_ulong(zt, val);
		break;
	case ZFS_TUNABLE_TYPE_U64:
		err = zfs_tunable_set_u64(zt, val);
		break;
	case ZFS_TUNABLE_TYPE_STRING:
		err = zfs_tunable_set_string(zt, val);
		break;
	default:
		err = EOPNOTSUPP;
		break;
	}
	return (err);
}

/* The public get function. Delegates to the type-specific version. */
int
zfs_tunable_get(const zfs_tunable_t *zt, char *val, size_t valsz)
{
	int err;
	switch (zt->zt_type) {
	case ZFS_TUNABLE_TYPE_INT:
		err = zfs_tunable_get_int(zt, val, valsz);
		break;
	case ZFS_TUNABLE_TYPE_UINT:
		err = zfs_tunable_get_uint(zt, val, valsz);
		break;
	case ZFS_TUNABLE_TYPE_ULONG:
		err = zfs_tunable_get_ulong(zt, val, valsz);
		break;
	case ZFS_TUNABLE_TYPE_U64:
		err = zfs_tunable_get_u64(zt, val, valsz);
		break;
	case ZFS_TUNABLE_TYPE_STRING:
		err = zfs_tunable_get_string(zt, val, valsz);
		break;
	default:
		err = EOPNOTSUPP;
		break;
	}
	return (err);
}
