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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 * Copyright (c) 2021, 2023, Klara Inc.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>

#include "zfs_prop.h"

#if !defined(_KERNEL)
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

static zprop_desc_t zpool_prop_table[ZPOOL_NUM_PROPS];
static zprop_desc_t vdev_prop_table[VDEV_NUM_PROPS];

zprop_desc_t *
zpool_prop_get_table(void)
{
	return (zpool_prop_table);
}

void
zpool_prop_init(void)
{
	static const zprop_index_t boolean_table[] = {
		{ "off",	0},
		{ "on",		1},
		{ NULL }
	};

	static const zprop_index_t failuremode_table[] = {
		{ "wait",	ZIO_FAILURE_MODE_WAIT },
		{ "continue",	ZIO_FAILURE_MODE_CONTINUE },
		{ "panic",	ZIO_FAILURE_MODE_PANIC },
		{ NULL }
	};

	struct zfs_mod_supported_features *sfeatures =
	    zfs_mod_list_supported(ZFS_SYSFS_POOL_PROPERTIES);

	/* string properties */
	zprop_register_string(ZPOOL_PROP_ALTROOT, "altroot", NULL, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<path>", "ALTROOT", sfeatures);
	zprop_register_string(ZPOOL_PROP_BOOTFS, "bootfs", NULL, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<filesystem>", "BOOTFS", sfeatures);
	zprop_register_string(ZPOOL_PROP_CACHEFILE, "cachefile", NULL,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<file> | none", "CACHEFILE",
	    sfeatures);
	zprop_register_string(ZPOOL_PROP_COMMENT, "comment", NULL,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<comment-string>", "COMMENT",
	    sfeatures);
	zprop_register_string(ZPOOL_PROP_COMPATIBILITY, "compatibility",
	    "off", PROP_DEFAULT, ZFS_TYPE_POOL,
	    "<file[,file...]> | off | legacy", "COMPATIBILITY", sfeatures);

	/* readonly number properties */
	zprop_register_number(ZPOOL_PROP_SIZE, "size", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "SIZE", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_FREE, "free", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "FREE", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_FREEING, "freeing", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "FREEING", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_CHECKPOINT, "checkpoint", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "CKPOINT", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_LEAKED, "leaked", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "LEAKED", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_ALLOCATED, "allocated", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "ALLOC", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_EXPANDSZ, "expandsize", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "EXPANDSZ", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_FRAGMENTATION, "fragmentation", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<percent>", "FRAG", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_CAPACITY, "capacity", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "CAP", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_GUID, "guid", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<guid>", "GUID", B_TRUE, sfeatures);
	zprop_register_number(ZPOOL_PROP_LOAD_GUID, "load_guid", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<load_guid>", "LOAD_GUID",
	    B_TRUE, sfeatures);
	zprop_register_number(ZPOOL_PROP_HEALTH, "health", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<state>", "HEALTH", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_DEDUPRATIO, "dedupratio", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<1.00x or higher if deduped>",
	    "DEDUP", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_BCLONEUSED, "bcloneused", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>",
	    "BCLONE_USED", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_BCLONESAVED, "bclonesaved", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>",
	    "BCLONE_SAVED", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_BCLONERATIO, "bcloneratio", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<1.00x or higher if cloned>",
	    "BCLONE_RATIO", B_FALSE, sfeatures);
	zprop_register_number(ZPOOL_PROP_DEDUP_TABLE_SIZE, "dedup_table_size",
	    0, PROP_READONLY, ZFS_TYPE_POOL, "<size>", "DDTSIZE", B_FALSE,
	    sfeatures);

	/* default number properties */
	zprop_register_number(ZPOOL_PROP_VERSION, "version", SPA_VERSION,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<version>", "VERSION", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_ASHIFT, "ashift", 0, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<ashift, 9-16, or 0=default>", "ASHIFT", B_FALSE,
	    sfeatures);
	zprop_register_number(ZPOOL_PROP_DEDUP_TABLE_QUOTA, "dedup_table_quota",
	    UINT64_MAX, PROP_DEFAULT, ZFS_TYPE_POOL, "<size>", "DDTQUOTA",
	    B_FALSE, sfeatures);

	/* default index (boolean) properties */
	zprop_register_index(ZPOOL_PROP_DELEGATION, "delegation", 1,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "DELEGATION",
	    boolean_table, sfeatures);
	zprop_register_index(ZPOOL_PROP_AUTOREPLACE, "autoreplace", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "REPLACE", boolean_table,
	    sfeatures);
	zprop_register_index(ZPOOL_PROP_LISTSNAPS, "listsnapshots", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "LISTSNAPS",
	    boolean_table, sfeatures);
	zprop_register_index(ZPOOL_PROP_AUTOEXPAND, "autoexpand", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "EXPAND", boolean_table,
	    sfeatures);
	zprop_register_index(ZPOOL_PROP_READONLY, "readonly", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "RDONLY", boolean_table,
	    sfeatures);
	zprop_register_index(ZPOOL_PROP_MULTIHOST, "multihost", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "MULTIHOST",
	    boolean_table, sfeatures);

	/* default index properties */
	zprop_register_index(ZPOOL_PROP_FAILUREMODE, "failmode",
	    ZIO_FAILURE_MODE_WAIT, PROP_DEFAULT, ZFS_TYPE_POOL,
	    "wait | continue | panic", "FAILMODE", failuremode_table,
	    sfeatures);
	zprop_register_index(ZPOOL_PROP_AUTOTRIM, "autotrim",
	    SPA_AUTOTRIM_OFF, PROP_DEFAULT, ZFS_TYPE_POOL,
	    "on | off", "AUTOTRIM", boolean_table, sfeatures);

	/* hidden properties */
	zprop_register_hidden(ZPOOL_PROP_NAME, "name", PROP_TYPE_STRING,
	    PROP_READONLY, ZFS_TYPE_POOL, "NAME", B_TRUE, sfeatures);
	zprop_register_hidden(ZPOOL_PROP_MAXBLOCKSIZE, "maxblocksize",
	    PROP_TYPE_NUMBER, PROP_READONLY, ZFS_TYPE_POOL, "MAXBLOCKSIZE",
	    B_FALSE, sfeatures);
	zprop_register_hidden(ZPOOL_PROP_TNAME, "tname", PROP_TYPE_STRING,
	    PROP_ONETIME, ZFS_TYPE_POOL, "TNAME", B_TRUE, sfeatures);
	zprop_register_hidden(ZPOOL_PROP_MAXDNODESIZE, "maxdnodesize",
	    PROP_TYPE_NUMBER, PROP_READONLY, ZFS_TYPE_POOL, "MAXDNODESIZE",
	    B_FALSE, sfeatures);
	zprop_register_hidden(ZPOOL_PROP_DEDUPDITTO, "dedupditto",
	    PROP_TYPE_NUMBER, PROP_DEFAULT, ZFS_TYPE_POOL, "DEDUPDITTO",
	    B_FALSE, sfeatures);
	zprop_register_hidden(ZPOOL_PROP_DEDUPCACHED,
	    ZPOOL_DEDUPCACHED_PROP_NAME, PROP_TYPE_NUMBER, PROP_READONLY,
	    ZFS_TYPE_POOL, "DEDUPCACHED", B_FALSE, sfeatures);

	zfs_mod_list_supported_free(sfeatures);
}

/*
 * Given a property name and its type, returns the corresponding property ID.
 */
zpool_prop_t
zpool_name_to_prop(const char *propname)
{
	return (zprop_name_to_prop(propname, ZFS_TYPE_POOL));
}

/*
 * Given a pool property ID, returns the corresponding name.
 * Assuming the pool property ID is valid.
 */
const char *
zpool_prop_to_name(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_name);
}

zprop_type_t
zpool_prop_get_type(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_proptype);
}

boolean_t
zpool_prop_readonly(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_attr == PROP_READONLY);
}

boolean_t
zpool_prop_setonce(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_attr == PROP_ONETIME);
}

const char *
zpool_prop_default_string(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_strdefault);
}

uint64_t
zpool_prop_default_numeric(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_numdefault);
}

/*
 * Returns true if this is a valid feature@ property.
 */
boolean_t
zpool_prop_feature(const char *name)
{
	static const char *prefix = "feature@";
	return (strncmp(name, prefix, strlen(prefix)) == 0);
}

/*
 * Returns true if this is a valid unsupported@ property.
 */
boolean_t
zpool_prop_unsupported(const char *name)
{
	static const char *prefix = "unsupported@";
	return (strncmp(name, prefix, strlen(prefix)) == 0);
}

int
zpool_prop_string_to_index(zpool_prop_t prop, const char *string,
    uint64_t *index)
{
	return (zprop_string_to_index(prop, string, index, ZFS_TYPE_POOL));
}

int
zpool_prop_index_to_string(zpool_prop_t prop, uint64_t index,
    const char **string)
{
	return (zprop_index_to_string(prop, index, string, ZFS_TYPE_POOL));
}

uint64_t
zpool_prop_random_value(zpool_prop_t prop, uint64_t seed)
{
	return (zprop_random_value(prop, seed, ZFS_TYPE_POOL));
}

#ifndef _KERNEL
#include <libzfs.h>

const char *
zpool_prop_values(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_values);
}

const char *
zpool_prop_column_name(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_colname);
}

boolean_t
zpool_prop_align_right(zpool_prop_t prop)
{
	return (zpool_prop_table[prop].pd_rightalign);
}
#endif

zprop_desc_t *
vdev_prop_get_table(void)
{
	return (vdev_prop_table);
}

void
vdev_prop_init(void)
{
	static const zprop_index_t boolean_table[] = {
		{ "off",	0},
		{ "on",		1},
		{ NULL }
	};
	static const zprop_index_t boolean_na_table[] = {
		{ "off",	0},
		{ "on",		1},
		{ "-",		2},	/* ZPROP_BOOLEAN_NA */
		{ NULL }
	};

	struct zfs_mod_supported_features *sfeatures =
	    zfs_mod_list_supported(ZFS_SYSFS_VDEV_PROPERTIES);

	/* string properties */
	zprop_register_string(VDEV_PROP_COMMENT, "comment", NULL,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<comment-string>", "COMMENT",
	    sfeatures);
	zprop_register_string(VDEV_PROP_PATH, "path", NULL,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<device-path>", "PATH", sfeatures);
	zprop_register_string(VDEV_PROP_DEVID, "devid", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<devid>", "DEVID", sfeatures);
	zprop_register_string(VDEV_PROP_PHYS_PATH, "physpath", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<physpath>", "PHYSPATH", sfeatures);
	zprop_register_string(VDEV_PROP_ENC_PATH, "encpath", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<encpath>", "ENCPATH", sfeatures);
	zprop_register_string(VDEV_PROP_FRU, "fru", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<fru>", "FRU", sfeatures);
	zprop_register_string(VDEV_PROP_PARENT, "parent", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<parent>", "PARENT", sfeatures);
	zprop_register_string(VDEV_PROP_CHILDREN, "children", NULL,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<child[,...]>", "CHILDREN",
	    sfeatures);

	/* readonly number properties */
	zprop_register_number(VDEV_PROP_SIZE, "size", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<size>", "SIZE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_FREE, "free", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<size>", "FREE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_ALLOCATED, "allocated", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<size>", "ALLOC", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_EXPANDSZ, "expandsize", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<size>", "EXPANDSZ", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_FRAGMENTATION, "fragmentation", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<percent>", "FRAG", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_CAPACITY, "capacity", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<size>", "CAP", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_GUID, "guid", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<guid>", "GUID", B_TRUE, sfeatures);
	zprop_register_number(VDEV_PROP_STATE, "state", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<state>", "STATE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_BOOTSIZE, "bootsize", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<size>", "BOOTSIZE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_ASIZE, "asize", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<asize>", "ASIZE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_PSIZE, "psize", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<psize>", "PSIZE", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_ASHIFT, "ashift", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<ashift>", "ASHIFT", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_PARITY, "parity", 0, PROP_READONLY,
	    ZFS_TYPE_VDEV, "<parity>", "PARITY", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_NUMCHILDREN, "numchildren", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<number-of-children>", "NUMCHILD",
	    B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_READ_ERRORS, "read_errors", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<errors>", "RDERR", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_WRITE_ERRORS, "write_errors", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<errors>", "WRERR", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_CHECKSUM_ERRORS, "checksum_errors", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<errors>", "CKERR", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_INITIALIZE_ERRORS,
	    "initialize_errors", 0, PROP_READONLY, ZFS_TYPE_VDEV, "<errors>",
	    "INITERR", B_FALSE, sfeatures);
	zprop_register_number(VDEV_PROP_TRIM_ERRORS, "trim_errors", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<errors>", "TRIMERR", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_SLOW_IOS, "slow_ios", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<slowios>", "SLOW", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_NULL, "null_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "NULLOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_READ, "read_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "READOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_WRITE, "write_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "WRITEOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_FREE, "free_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "FREEOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_CLAIM, "claim_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "CLAIMOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_OPS_TRIM, "trim_ops", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<operations>", "TRIMOP", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_NULL, "null_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "NULLBYTE", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_READ, "read_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "READBYTE", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_WRITE, "write_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "WRITEBYTE", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_FREE, "free_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "FREEBYTE", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_CLAIM, "claim_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "CLAIMBYTE", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_BYTES_TRIM, "trim_bytes", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "<bytes>", "TRIMBYTE", B_FALSE,
	    sfeatures);

	/* default numeric properties */
	zprop_register_number(VDEV_PROP_CHECKSUM_N, "checksum_n", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<events>", "CKSUM_N", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_CHECKSUM_T, "checksum_t", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<seconds>", "CKSUM_T", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_IO_N, "io_n", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<events>", "IO_N", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_IO_T, "io_t", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<seconds>", "IO_T", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_SLOW_IO_N, "slow_io_n", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<events>", "SLOW_IO_N", B_FALSE,
	    sfeatures);
	zprop_register_number(VDEV_PROP_SLOW_IO_T, "slow_io_t", UINT64_MAX,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "<seconds>", "SLOW_IO_T", B_FALSE,
	    sfeatures);

	/* default index (boolean) properties */
	zprop_register_index(VDEV_PROP_REMOVING, "removing", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "on | off", "REMOVING",
	    boolean_table, sfeatures);
	zprop_register_index(VDEV_PROP_ALLOCATING, "allocating", 1,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "on | off", "ALLOCATING",
	    boolean_na_table, sfeatures);
	zprop_register_index(VDEV_PROP_RAIDZ_EXPANDING, "raidz_expanding", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "on | off", "RAIDZ_EXPANDING",
	    boolean_table, sfeatures);
	zprop_register_index(VDEV_PROP_TRIM_SUPPORT, "trim_support", 0,
	    PROP_READONLY, ZFS_TYPE_VDEV, "on | off", "TRIMSUP",
	    boolean_table, sfeatures);

	/* default index properties */
	zprop_register_index(VDEV_PROP_FAILFAST, "failfast", B_TRUE,
	    PROP_DEFAULT, ZFS_TYPE_VDEV, "on | off", "FAILFAST", boolean_table,
	    sfeatures);

	/* hidden properties */
	zprop_register_hidden(VDEV_PROP_NAME, "name", PROP_TYPE_STRING,
	    PROP_READONLY, ZFS_TYPE_VDEV, "NAME", B_TRUE, sfeatures);

	zfs_mod_list_supported_free(sfeatures);
}

/*
 * Given a property name and its type, returns the corresponding property ID.
 */
vdev_prop_t
vdev_name_to_prop(const char *propname)
{
	return (zprop_name_to_prop(propname, ZFS_TYPE_VDEV));
}

/*
 * Returns true if this is a valid user-defined property (one with a ':').
 */
boolean_t
vdev_prop_user(const char *name)
{
	int i, len;
	char c;
	boolean_t foundsep = B_FALSE;

	len = strlen(name);
	for (i = 0; i < len; i++) {
		c = name[i];
		if (!zprop_valid_char(c))
			return (B_FALSE);
		if (c == ':')
			foundsep = B_TRUE;
	}

	return (foundsep);
}

/*
 * Given a pool property ID, returns the corresponding name.
 * Assuming the pool property ID is valid.
 */
const char *
vdev_prop_to_name(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_name);
}

zprop_type_t
vdev_prop_get_type(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_proptype);
}

boolean_t
vdev_prop_readonly(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_attr == PROP_READONLY);
}

const char *
vdev_prop_default_string(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_strdefault);
}

uint64_t
vdev_prop_default_numeric(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_numdefault);
}

int
vdev_prop_string_to_index(vdev_prop_t prop, const char *string,
    uint64_t *index)
{
	return (zprop_string_to_index(prop, string, index, ZFS_TYPE_VDEV));
}

int
vdev_prop_index_to_string(vdev_prop_t prop, uint64_t index,
    const char **string)
{
	return (zprop_index_to_string(prop, index, string, ZFS_TYPE_VDEV));
}

/*
 * Returns true if this is a valid vdev property.
 */
boolean_t
zpool_prop_vdev(const char *name)
{
	return (vdev_name_to_prop(name) != VDEV_PROP_INVAL);
}

uint64_t
vdev_prop_random_value(vdev_prop_t prop, uint64_t seed)
{
	return (zprop_random_value(prop, seed, ZFS_TYPE_VDEV));
}

#ifndef _KERNEL
const char *
vdev_prop_values(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_values);
}

const char *
vdev_prop_column_name(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_colname);
}

boolean_t
vdev_prop_align_right(vdev_prop_t prop)
{
	return (vdev_prop_table[prop].pd_rightalign);
}
#endif

#if defined(_KERNEL)
/* zpool property functions */
EXPORT_SYMBOL(zpool_prop_init);
EXPORT_SYMBOL(zpool_prop_get_type);
EXPORT_SYMBOL(zpool_prop_get_table);

/* vdev property functions */
EXPORT_SYMBOL(vdev_prop_init);
EXPORT_SYMBOL(vdev_prop_get_type);
EXPORT_SYMBOL(vdev_prop_get_table);

/* Pool property functions shared between libzfs and kernel. */
EXPORT_SYMBOL(zpool_name_to_prop);
EXPORT_SYMBOL(zpool_prop_to_name);
EXPORT_SYMBOL(zpool_prop_default_string);
EXPORT_SYMBOL(zpool_prop_default_numeric);
EXPORT_SYMBOL(zpool_prop_readonly);
EXPORT_SYMBOL(zpool_prop_feature);
EXPORT_SYMBOL(zpool_prop_unsupported);
EXPORT_SYMBOL(zpool_prop_index_to_string);
EXPORT_SYMBOL(zpool_prop_string_to_index);
EXPORT_SYMBOL(zpool_prop_vdev);

/* vdev property functions shared between libzfs and kernel. */
EXPORT_SYMBOL(vdev_name_to_prop);
EXPORT_SYMBOL(vdev_prop_user);
EXPORT_SYMBOL(vdev_prop_to_name);
EXPORT_SYMBOL(vdev_prop_default_string);
EXPORT_SYMBOL(vdev_prop_default_numeric);
EXPORT_SYMBOL(vdev_prop_readonly);
EXPORT_SYMBOL(vdev_prop_index_to_string);
EXPORT_SYMBOL(vdev_prop_string_to_index);
#endif
