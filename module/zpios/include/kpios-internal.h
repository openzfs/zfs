/*
 * This file is part of the ZFS Linux port.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 * LLNL-CODE-403049
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#ifndef _KPIOS_INTERNAL_H
#define _KPIOS_INTERNAL_H

#include "kpios-ctl.h"

#define OBJ_SIZE	64

struct run_args;

typedef struct dmu_obj {
	objset_t *os;
	uint64_t obj;
} dmu_obj_t;

/* thread doing the IO data */
typedef struct thread_data {
	struct run_args *run_args;
	int thread_no;
	int rc;
	kpios_stats_t stats;
        kmutex_t lock;
} thread_data_t;

/* region for IO data */
typedef struct kpios_region {
	__u64 wr_offset;
	__u64 rd_offset;
	__u64 init_offset;
	__u64 max_offset;
	dmu_obj_t obj;
	kpios_stats_t stats;
        kmutex_t lock;
} kpios_region_t;

/* arguments for one run */
typedef struct run_args {
	/* Config args */
	int id;
	char pool[KPIOS_NAME_SIZE];
	__u64 chunk_size;
	__u32 thread_count;
	__u32 region_count;
	__u64 region_size;
	__u64 offset;
	__u32 region_noise;
	__u32 chunk_noise;
	__u32 thread_delay;
	__u32 flags;
	char pre[KPIOS_PATH_SIZE];
	char post[KPIOS_PATH_SIZE];
	char log[KPIOS_PATH_SIZE];

	/* Control data */
	objset_t *os;
        wait_queue_head_t waitq;
	volatile uint64_t threads_done;
        kmutex_t lock_work;
	kmutex_t lock_ctl;
	__u32 region_next;

	/* Results data */
	struct file *file;
	kpios_stats_t stats;

	thread_data_t **threads;
	kpios_region_t regions[0]; /* Must be last element */
} run_args_t;

#define KPIOS_INFO_BUFFER_SIZE          65536
#define KPIOS_INFO_BUFFER_REDZONE       1024

typedef struct kpios_info {
        spinlock_t info_lock;
        int info_size;
        char *info_buffer;
        char *info_head;        /* Internal kernel use only */
} kpios_info_t;

#define kpios_print(file, format, args...)                              \
({      kpios_info_t *_info_ = (kpios_info_t *)file->private_data;      \
        int _rc_;                                                       \
                                                                        \
        ASSERT(_info_);                                                 \
        ASSERT(_info_->info_buffer);                                    \
                                                                        \
        spin_lock(&_info_->info_lock);                                  \
                                                                        \
        /* Don't allow the kernel to start a write in the red zone */   \
        if ((int)(_info_->info_head - _info_->info_buffer) >            \
            (_info_->info_size - KPIOS_INFO_BUFFER_REDZONE))      {     \
                _rc_ = -EOVERFLOW;                                      \
        } else {                                                        \
                _rc_ = sprintf(_info_->info_head, format, args);        \
                if (_rc_ >= 0)                                          \
                        _info_->info_head += _rc_;                      \
        }                                                               \
                                                                        \
        spin_unlock(&_info_->info_lock);                                \
        _rc_;                                                           \
})

#define kpios_vprint(file, test, format, args...)                       \
        kpios_print(file, "%*s: " format, KPIOS_NAME_SIZE, test, args)

#endif /* _KPIOS_INTERNAL_H */
