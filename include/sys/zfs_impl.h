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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	_SYS_ZFS_IMPL_H
#define	_SYS_ZFS_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/* generic implementation backends */
typedef struct
{
	/* algorithm name */
	const char *name;

	/* get number of supported implementations */
	uint32_t (*getcnt)(void);

	/* get id of selected implementation */
	uint32_t (*getid)(void);

	/* get name of selected implementation */
	const char *(*getname)(void);

	/* setup id as fastest implementation */
	void (*set_fastest)(uint32_t id);

	/* set implementation by id */
	void (*setid)(uint32_t id);

	/* set implementation by name */
	int (*setname)(const char *val);
} zfs_impl_t;

/* return some set of function pointer */
extern const zfs_impl_t *zfs_impl_get_ops(const char *algo);

extern const zfs_impl_t zfs_blake3_ops;
extern const zfs_impl_t zfs_sha256_ops;
extern const zfs_impl_t zfs_sha512_ops;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IMPL_H */
