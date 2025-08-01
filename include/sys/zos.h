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
 * Copyright (c) 2025 Stephen Reaves. All rights reserved.
 */

/*
 * The objective of this program is to provide an S3-compatible object storage,
 * bypassing the ZPL, and interacting directly with the DMU.
 */

#ifndef _SYS_ZOS_H
#define _SYS_ZOS_H

#include <sys/param.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_ZOS_PREFIX "zos"

// TODO: figure out where to source this ...
#ifndef MAXNAMELEN
#define MAXNAMELEN 256
#endif

struct zos_object {
  char *bucket_name;
  char *object_name;
  void *data;
  size_t data_size;
};

int create_bucket(const char *bucket);
int delete_bucket(const char *bucket);

int upsert_object(const struct zos_object *object, void *object_data,
                  size_t object_data_size);
int read_object(struct zos_object *object);
int delete_object(struct zos_object *object);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ZOS_H */
