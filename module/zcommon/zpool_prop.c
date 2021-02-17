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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
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

zprop_desc_t *
zpool_prop_get_table(void)
{
	return (zpool_prop_table);
}

void
zpool_prop_init(void)
{
	static zprop_index_t boolean_table[] = {
		{ "off",	0},
		{ "on",		1},
		{ NULL }
	};

	static zprop_index_t failuremode_table[] = {
		{ "wait",	ZIO_FAILURE_MODE_WAIT },
		{ "continue",	ZIO_FAILURE_MODE_CONTINUE },
		{ "panic",	ZIO_FAILURE_MODE_PANIC },
		{ NULL }
	};

	/* string properties */
	zprop_register_string(ZPOOL_PROP_ALTROOT, "altroot", NULL, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<path>", "ALTROOT");
	zprop_register_string(ZPOOL_PROP_BOOTFS, "bootfs", NULL, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<filesystem>", "BOOTFS");
	zprop_register_string(ZPOOL_PROP_CACHEFILE, "cachefile", NULL,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<file> | none", "CACHEFILE");
	zprop_register_string(ZPOOL_PROP_COMMENT, "comment", NULL,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<comment-string>", "COMMENT");
	zprop_register_string(ZPOOL_PROP_COMPATIBILITY, "compatibility",
	    "off", PROP_DEFAULT, ZFS_TYPE_POOL,
	    "<file[,file...]> | off | legacy", "COMPATIBILITY");

	/* readonly number properties */
	zprop_register_number(ZPOOL_PROP_SIZE, "size", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "SIZE");
	zprop_register_number(ZPOOL_PROP_FREE, "free", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "FREE");
	zprop_register_number(ZPOOL_PROP_FREEING, "freeing", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "FREEING");
	zprop_register_number(ZPOOL_PROP_CHECKPOINT, "checkpoint", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "CKPOINT");
	zprop_register_number(ZPOOL_PROP_LEAKED, "leaked", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "LEAKED");
	zprop_register_number(ZPOOL_PROP_ALLOCATED, "allocated", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "ALLOC");
	zprop_register_number(ZPOOL_PROP_EXPANDSZ, "expandsize", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<size>", "EXPANDSZ");
	zprop_register_number(ZPOOL_PROP_FRAGMENTATION, "fragmentation", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<percent>", "FRAG");
	zprop_register_number(ZPOOL_PROP_CAPACITY, "capacity", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<size>", "CAP");
	zprop_register_number(ZPOOL_PROP_GUID, "guid", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<guid>", "GUID");
	zprop_register_number(ZPOOL_PROP_LOAD_GUID, "load_guid", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<load_guid>", "LOAD_GUID");
	zprop_register_number(ZPOOL_PROP_HEALTH, "health", 0, PROP_READONLY,
	    ZFS_TYPE_POOL, "<state>", "HEALTH");
	zprop_register_number(ZPOOL_PROP_DEDUPRATIO, "dedupratio", 0,
	    PROP_READONLY, ZFS_TYPE_POOL, "<1.00x or higher if deduped>",
	    "DEDUP");

	/* default number properties */
	zprop_register_number(ZPOOL_PROP_VERSION, "version", SPA_VERSION,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "<version>", "VERSION");
	zprop_register_number(ZPOOL_PROP_ASHIFT, "ashift", 0, PROP_DEFAULT,
	    ZFS_TYPE_POOL, "<ashift, 9-16, or 0=default>", "ASHIFT");

	/* default index (boolean) properties */
	zprop_register_index(ZPOOL_PROP_DELEGATION, "delegation", 1,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "DELEGATION",
	    boolean_table);
	zprop_register_index(ZPOOL_PROP_AUTOREPLACE, "autoreplace", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "REPLACE", boolean_table);
	zprop_register_index(ZPOOL_PROP_LISTSNAPS, "listsnapshots", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "LISTSNAPS",
	    boolean_table);
	zprop_register_index(ZPOOL_PROP_AUTOEXPAND, "autoexpand", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "EXPAND", boolean_table);
	zprop_register_index(ZPOOL_PROP_READONLY, "readonly", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "RDONLY", boolean_table);
	zprop_register_index(ZPOOL_PROP_MULTIHOST, "multihost", 0,
	    PROP_DEFAULT, ZFS_TYPE_POOL, "on | off", "MULTIHOST",
	    boolean_table);

	/* default index properties */
	zprop_register_index(ZPOOL_PROP_FAILUREMODE, "failmode",
	    ZIO_FAILURE_MODE_WAIT, PROP_DEFAULT, ZFS_TYPE_POOL,
	    "wait | continue | panic", "FAILMODE", failuremode_table);
	zprop_register_index(ZPOOL_PROP_AUTOTRIM, "autotrim",
	    SPA_AUTOTRIM_DEFAULT, PROP_DEFAULT, ZFS_TYPE_POOL,
	    "on | off", "AUTOTRIM", boolean_table);

	/* hidden properties */
	zprop_register_hidden(ZPOOL_PROP_NAME, "name", PROP_TYPE_STRING,
	    PROP_READONLY, ZFS_TYPE_POOL, "NAME");
	zprop_register_hidden(ZPOOL_PROP_MAXBLOCKSIZE, "maxblocksize",
	    PROP_TYPE_NUMBER, PROP_READONLY, ZFS_TYPE_POOL, "MAXBLOCKSIZE");
	zprop_register_hidden(ZPOOL_PROP_TNAME, "tname", PROP_TYPE_STRING,
	    PROP_ONETIME, ZFS_TYPE_POOL, "TNAME");
	zprop_register_hidden(ZPOOL_PROP_MAXDNODESIZE, "maxdnodesize",
	    PROP_TYPE_NUMBER, PROP_READONLY, ZFS_TYPE_POOL, "MAXDNODESIZE");
	zprop_register_hidden(ZPOOL_PROP_DEDUPDITTO, "dedupditto",
	    PROP_TYPE_NUMBER, PROP_DEFAULT, ZFS_TYPE_POOL, "DEDUPDITTO");
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

#if defined(_KERNEL)
/* zpool property functions */
EXPORT_SYMBOL(zpool_prop_init);
EXPORT_SYMBOL(zpool_prop_get_type);
EXPORT_SYMBOL(zpool_prop_get_table);

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
#endif
