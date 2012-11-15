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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Gunnar Beutner
 */

typedef struct iscsi_target_s {
        int     tid;            /* Target ID */
        char    name[255];      /* Target Name */
        int     lun;            /* Target LUN */
        int     state;          /* Target State */
        char    iotype[3];      /* Target IO Type */
        char    iomode[20];     /* Target IO Mode */
        int     blocks;         /* Target Size (blocks) */
        int     blocksize;      /* Target Block Size (bytes) */
        char    path[255];      /* Target Path */

        struct iscsi_target_s *next;
} iscsi_target_t;

iscsi_target_t *iscsi_targets;

void libshare_iscsi_init(void);
int iscsi_generate_target(const char *, char *, size_t);
int iscsi_enable_share_one(int, char *, const char *, const char *);
int iscsi_disable_share_one(int);
int iscsi_disable_share_all(void);
