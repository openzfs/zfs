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
 * Copyright (c) 2025 Stephen Reaves. All rights reserved.
 */

/*
 * The objective of this program is to provide an S3-compatible object storage,
 * bypassing the ZPL, and interacting directly with the DMU.
 */

#ifndef _SYS_ZOS_H
#define	_SYS_ZOS_H

#include <sys/param.h>
#include <sys/types.h>
#include <sys/dmu.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Suffix appended to pool name for the hidden ZOS objset (e.g. "tank/__zos") */
#define	ZOS_OBJSET_SUFFIX	"__zos"

/* Well-known object ID inside the ZOS objset: bucket directory ZAP */
#define	ZOS_BUCKET_DIR_OBJ	1ULL

// TODO: figure out where to source this ...
#ifndef MAXNAMELEN
#define	MAXNAMELEN 256
#endif

typedef struct zos_object_meta {
	uint64_t size;
} zos_object_meta_t;

int zos_get_objset(spa_t *spa, objset_t **osp);
void zos_release_objset(objset_t *os);
int create_bucket(const char *pool, const char *bucket);
int delete_bucket(const char *pool, const char *bucket);
/* Resolved bucket: held objset + bucket ZAP object ID */
int get_bucket(spa_t *spa, const char *bucket, objset_t **os,
    uint64_t *bucket_zap);

/*
 * Put (create/overwrite) an object.  fd = data source, size = bytes
 * (0 = read until EOF).
 */
int put_object(const char *pool, const char *bucket, const char *key,
    int fd, uint64_t size);
int delete_object(const char *pool, const char *bucket, const char *key);
int get_object(const char *pool, const char *bucket, const char *key,
    int fd, uint64_t *size);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ZOS_H */
