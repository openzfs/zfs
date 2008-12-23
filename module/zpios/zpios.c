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
 *
 * Kernel PIOS DMU implemenation originally derived from PIOS test code.
 * Character control interface derived from SPL code.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <linux/cdev.h>
#include "zpios-internal.h"


static struct class *kpios_class;


static inline
struct timespec timespec_add(struct timespec lhs, struct timespec rhs)
{
        struct timespec ts_delta;
        set_normalized_timespec(&ts_delta, lhs.tv_sec + rhs.tv_sec,
                                lhs.tv_nsec + rhs.tv_nsec);
        return ts_delta;
}

static
int kpios_upcall(char *path, char *phase, run_args_t *run_args, int rc)
{
	/* This is stack heavy but it should be OK since we are only
	 * making the upcall between tests when the stack is shallow.
	 */
        char id[16], chunk_size[16], region_size[16], thread_count[16];
	char region_count[16], offset[16], region_noise[16], chunk_noise[16];
        char thread_delay[16], flags[16], result[8];
        char *argv[16], *envp[4];

	if (strlen(path) == 0)
		return -ENOENT;

	snprintf(id, 15, "%d", run_args->id);
	snprintf(chunk_size, 15, "%lu", (long unsigned)run_args->chunk_size);
        snprintf(region_size, 15, "%lu",(long unsigned) run_args->region_size);
	snprintf(thread_count, 15, "%u", run_args->thread_count);
	snprintf(region_count, 15, "%u", run_args->region_count);
	snprintf(offset, 15, "%lu", (long unsigned)run_args->offset);
	snprintf(region_noise, 15, "%u", run_args->region_noise);
	snprintf(chunk_noise, 15, "%u", run_args->chunk_noise);
	snprintf(thread_delay, 15, "%u", run_args->thread_delay);
	snprintf(flags, 15, "0x%x", run_args->flags);
	snprintf(result, 7, "%d", rc);

	/* Passing 15 args to registered pre/post upcall */
        argv[0] = path;
	argv[1] = phase;
	argv[2] = strlen(run_args->log) ? run_args->log : "<none>";
	argv[3] = id;
	argv[4] = run_args->pool;
	argv[5] = chunk_size;
	argv[6] = region_size;
	argv[7] = thread_count;
	argv[8] = region_count;
	argv[9] = offset;
	argv[10] = region_noise;
	argv[11] = chunk_noise;
	argv[12] = thread_delay;
	argv[13] = flags;
	argv[14] = result;
	argv[15] = NULL;

	/* Passing environment for userspace upcall */
        envp[0] = "HOME=/";
        envp[1] = "TERM=linux";
        envp[2] = "PATH=/sbin:/usr/sbin:/bin:/usr/bin";
        envp[3] = NULL;

        return call_usermodehelper(path, argv, envp, 1);
}

static uint64_t
kpios_dmu_object_create(run_args_t *run_args, objset_t *os)
{
	struct dmu_tx *tx;
        uint64_t obj = 0ULL;
	int rc;

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, OBJ_SIZE);
	rc = dmu_tx_assign(tx, TXG_WAIT);
	if (rc) {
		kpios_print(run_args->file,
			    "dmu_tx_assign() failed: %d\n", rc);
		dmu_tx_abort(tx);
		return obj;
	}

	obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0,
	                       DMU_OT_NONE, 0, tx);
	rc = dmu_object_set_blocksize(os, obj, 128ULL << 10, 0, tx);
	if (rc) {
		kpios_print(run_args->file,
			    "dmu_object_set_blocksize() failed: %d\n", rc);
	        dmu_tx_abort(tx);
	        return obj;
	}

	dmu_tx_commit(tx);

	return obj;
}

static int
kpios_dmu_object_free(run_args_t *run_args, objset_t *os, uint64_t obj)
{
	struct dmu_tx *tx;
	int rc;

	tx = dmu_tx_create(os);
        dmu_tx_hold_free(tx, obj, 0, DMU_OBJECT_END);
	rc = dmu_tx_assign(tx, TXG_WAIT);
	if (rc) {
		kpios_print(run_args->file,
			    "dmu_tx_assign() failed: %d\n", rc);
		dmu_tx_abort(tx);
		return rc;
	}

	rc = dmu_object_free(os, obj, tx);
	if (rc) {
		kpios_print(run_args->file,
			    "dmu_object_free() failed: %d\n", rc);
	        dmu_tx_abort(tx);
	        return rc;
	}

	dmu_tx_commit(tx);

	return 0;
}

static int
kpios_dmu_setup(run_args_t *run_args)
{
	kpios_time_t *t = &(run_args->stats.cr_time);
	objset_t *os;
	uint64_t obj = 0ULL;
	int i, rc = 0;

	t->start = current_kernel_time();

        rc = dmu_objset_open(run_args->pool, DMU_OST_ZFS, DS_MODE_STANDARD, &os);
        if (rc) {
		kpios_print(run_args->file, "Error dmu_objset_open() "
			    "failed: %d\n", rc);
		goto out;
        }

	if (!(run_args->flags & DMU_FPP)) {
		obj = kpios_dmu_object_create(run_args, os);
		if (obj == 0) {
			rc = -EBADF;
			kpios_print(run_args->file, "Error kpios_dmu_"
				    "object_create() failed, %d\n", rc);
			goto out;
		}
	}

	for (i = 0; i < run_args->region_count; i++) {
		kpios_region_t *region;

		region = &run_args->regions[i];
	        mutex_init(&region->lock, NULL, MUTEX_DEFAULT, NULL);

		if (run_args->flags & DMU_FPP) {
			/* File per process */
			region->obj.os  = os;
			region->obj.obj = kpios_dmu_object_create(run_args, os);
			ASSERT(region->obj.obj > 0); /* XXX - Handle this */
			region->wr_offset   = run_args->offset;
			region->rd_offset   = run_args->offset;
			region->init_offset = run_args->offset;
			region->max_offset  = run_args->offset +
			                      run_args->region_size;
		} else {
			/* Single shared file */
			region->obj.os  = os;
			region->obj.obj = obj;
			region->wr_offset   = run_args->offset * i;
			region->rd_offset   = run_args->offset * i;
			region->init_offset = run_args->offset * i;
			region->max_offset  = run_args->offset *
			                      i + run_args->region_size;
		}
	}

	run_args->os = os;
out:
	t->stop = current_kernel_time();
	t->delta = timespec_sub(t->stop, t->start);

	return rc;
}

static int
kpios_setup_run(run_args_t **run_args, kpios_cmd_t *kcmd, struct file *file)
{
	run_args_t *ra;
	int rc, size;

	size = sizeof(*ra) + kcmd->cmd_region_count * sizeof(kpios_region_t);

	ra = vmem_zalloc(size, KM_SLEEP);
	if (ra == NULL) {
		kpios_print(file, "Unable to vmem_zalloc() %d bytes "
			    "for regions\n", size);
		return -ENOMEM;
	}

	*run_args = ra;
	strncpy(ra->pool, kcmd->cmd_pool, KPIOS_NAME_SIZE - 1);
	strncpy(ra->pre, kcmd->cmd_pre, KPIOS_PATH_SIZE - 1);
	strncpy(ra->post, kcmd->cmd_post, KPIOS_PATH_SIZE - 1);
	strncpy(ra->log, kcmd->cmd_log, KPIOS_PATH_SIZE - 1);
	ra->id              = kcmd->cmd_id;
	ra->chunk_size      = kcmd->cmd_chunk_size;
	ra->thread_count    = kcmd->cmd_thread_count;
	ra->region_count    = kcmd->cmd_region_count;
	ra->region_size     = kcmd->cmd_region_size;
	ra->offset          = kcmd->cmd_offset;
	ra->region_noise    = kcmd->cmd_region_noise;
	ra->chunk_noise     = kcmd->cmd_chunk_noise;
	ra->thread_delay    = kcmd->cmd_thread_delay;
	ra->flags           = kcmd->cmd_flags;
	ra->stats.wr_data   = 0;
	ra->stats.wr_chunks = 0;
	ra->stats.rd_data   = 0;
	ra->stats.rd_chunks = 0;
	ra->region_next     = 0;
	ra->file            = file;
        mutex_init(&ra->lock_work, NULL, MUTEX_DEFAULT, NULL);
        mutex_init(&ra->lock_ctl, NULL, MUTEX_DEFAULT, NULL);

	rc = kpios_dmu_setup(ra);
	if (rc) {
	        mutex_destroy(&ra->lock_ctl);
	        mutex_destroy(&ra->lock_work);
		vmem_free(ra, size);
		*run_args = NULL;
	}

	return rc;
}

static int
kpios_get_work_item(run_args_t *run_args, dmu_obj_t *obj, __u64 *offset,
		    __u32 *chunk_size, kpios_region_t **region, __u32 flags)
{
	int i, j, count = 0;
	unsigned int random_int;

	get_random_bytes(&random_int, sizeof(unsigned int));

	mutex_enter(&run_args->lock_work);
	i = run_args->region_next;

	/* XXX: I don't much care for this chunk selection mechansim
	 * there's the potential to burn a lot of time here doing nothing
	 * useful while holding the global lock.  This could give some
	 * misleading performance results.  I'll fix it latter.
	 */
	while (count < run_args->region_count) {
		__u64 *rw_offset;
		kpios_time_t *rw_time;

		j = i % run_args->region_count;
		*region = &(run_args->regions[j]);

		if (flags & DMU_WRITE) {
			rw_offset = &((*region)->wr_offset);
			rw_time = &((*region)->stats.wr_time);
		} else {
			rw_offset = &((*region)->rd_offset);
			rw_time = &((*region)->stats.rd_time);
		}

		/* test if region is fully written */
		if (*rw_offset + *chunk_size > (*region)->max_offset) {
			i++;
			count++;

			if (unlikely(rw_time->stop.tv_sec == 0) &&
			    unlikely(rw_time->stop.tv_nsec == 0))
				rw_time->stop = current_kernel_time();

			continue;
		}

		*offset = *rw_offset;
		*obj = (*region)->obj;
		*rw_offset += *chunk_size;

		/* update ctl structure */
		if (run_args->region_noise) {
			get_random_bytes(&random_int, sizeof(unsigned int));
	                run_args->region_next += random_int % run_args->region_noise;
		} else {
			run_args->region_next++;
		}

		mutex_exit(&run_args->lock_work);
		return 1;
	}

	/* nothing left to do */
	mutex_exit(&run_args->lock_work);

	return 0;
}

static void
kpios_remove_objects(run_args_t *run_args)
{
	kpios_time_t *t = &(run_args->stats.rm_time);
	kpios_region_t *region;
	int rc = 0, i;

	t->start = current_kernel_time();

	if (run_args->flags & DMU_REMOVE) {
		if (run_args->flags & DMU_FPP) {
			for (i = 0; i < run_args->region_count; i++) {
				region = &run_args->regions[i];
				rc = kpios_dmu_object_free(run_args,
							   region->obj.os,
							   region->obj.obj);
				if (rc)
					kpios_print(run_args->file, "Error "
						    "removing object %d, %d\n",
					            (int)region->obj.obj, rc);
			}
		} else {
			region = &run_args->regions[0];
			rc = kpios_dmu_object_free(run_args,
						   region->obj.os,
						   region->obj.obj);
			if (rc)
				kpios_print(run_args->file, "Error "
					    "removing object %d, %d\n",
				            (int)region->obj.obj, rc);
		}
	}

	dmu_objset_close(run_args->os);

	t->stop = current_kernel_time();
	t->delta = timespec_sub(t->stop, t->start);
}

static void
kpios_cleanup_run(run_args_t *run_args)
{
	int i, size = 0;

	if (run_args == NULL)
		return;

	if (run_args->threads != NULL) {
		for (i = 0; i < run_args->thread_count; i++) {
			if (run_args->threads[i]) {
				mutex_destroy(&run_args->threads[i]->lock);
				kmem_free(run_args->threads[i],
					  sizeof(thread_data_t));
			}
		}

		kmem_free(run_args->threads,
			  sizeof(thread_data_t *) * run_args->thread_count);
	}

	if (run_args->regions != NULL)
		for (i = 0; i < run_args->region_count; i++)
			mutex_destroy(&run_args->regions[i].lock);

        mutex_destroy(&run_args->lock_work);
        mutex_destroy(&run_args->lock_ctl);

	if (run_args->regions != NULL)
		size = run_args->region_count * sizeof(kpios_region_t);

	vmem_free(run_args, sizeof(*run_args) + size);
}

static int
kpios_dmu_write(run_args_t *run_args, objset_t *os, uint64_t object,
		uint64_t offset, uint64_t size, const void *buf)
{
        struct dmu_tx *tx;
        int rc, how = TXG_WAIT;
	int flags = 0;

        while (1) {
                tx = dmu_tx_create(os);
                dmu_tx_hold_write(tx, object, offset, size);
                rc = dmu_tx_assign(tx, how);

                if (rc) {
                        if (rc == ERESTART && how == TXG_NOWAIT) {
                                dmu_tx_wait(tx);
                                dmu_tx_abort(tx);
                                continue;
                        }
                        kpios_print(run_args->file,
				    "Error in dmu_tx_assign(), %d", rc);
                        dmu_tx_abort(tx);
                        return rc;
                }
                break;
        }

	if (run_args->flags & DMU_WRITE_ZC)
		flags |= DMU_WRITE_ZEROCOPY;

        dmu_write_impl(os, object, offset, size, buf, tx, flags);
        dmu_tx_commit(tx);

        return 0;
}

static int
kpios_dmu_read(run_args_t *run_args, objset_t *os, uint64_t object,
	       uint64_t offset, uint64_t size, void *buf)
{
	int flags = 0;

	if (run_args->flags & DMU_READ_ZC)
		flags |= DMU_READ_ZEROCOPY;

	return dmu_read_impl(os, object, offset, size, buf, flags);
}

static int
kpios_thread_main(void *data)
{
	thread_data_t *thr = (thread_data_t *)data;
	run_args_t *run_args = thr->run_args;
	kpios_time_t t;

        dmu_obj_t obj;
        __u64 offset;
	__u32 chunk_size;
	kpios_region_t *region;
        char *buf;

	unsigned int random_int;
        int chunk_noise = run_args->chunk_noise;
        int chunk_noise_tmp = 0;
        int thread_delay = run_args->thread_delay;
        int thread_delay_tmp = 0;

        int i, rc = 0;

        if (chunk_noise) {
		get_random_bytes(&random_int, sizeof(unsigned int));
                chunk_noise_tmp = (random_int % (chunk_noise * 2)) - chunk_noise;
        }

	/* It's OK to vmem_alloc() this memory because it will be copied
	 * in to the slab and pointers to the slab copy will be setup in
	 * the bio when the IO is submitted.  This of course is not ideal
	 * since we want a zero-copy IO path if possible.  It would be nice
	 * to have direct access to those slab entries.
	 */
	chunk_size = run_args->chunk_size + chunk_noise_tmp;
	buf = (char *)vmem_alloc(chunk_size, KM_SLEEP);
	ASSERT(buf);

	/* Trivial data verification pattern for now. */
	if (run_args->flags & DMU_VERIFY)
		memset(buf, 'z', chunk_size);

	/* Write phase */
	mutex_enter(&thr->lock);
        thr->stats.wr_time.start = current_kernel_time();
	mutex_exit(&thr->lock);

        while (kpios_get_work_item(run_args, &obj, &offset,
				   &chunk_size, &region, DMU_WRITE)) {
		if (thread_delay) {
			get_random_bytes(&random_int, sizeof(unsigned int));
	                thread_delay_tmp = random_int % thread_delay;
		        set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(thread_delay_tmp); /* In jiffies */
		}

	        t.start = current_kernel_time();
		rc = kpios_dmu_write(run_args, obj.os, obj.obj,
				     offset, chunk_size, buf);
	        t.stop = current_kernel_time();
		t.delta = timespec_sub(t.stop, t.start);

		if (rc) {
			kpios_print(run_args->file, "IO error while doing "
				    "dmu_write(): %d\n", rc);
			break;
		}

		mutex_enter(&thr->lock);
		thr->stats.wr_data += chunk_size;
		thr->stats.wr_chunks++;
		thr->stats.wr_time.delta = timespec_add(
		        thr->stats.wr_time.delta, t.delta);
		mutex_exit(&thr->lock);

		mutex_enter(&region->lock);
		region->stats.wr_data += chunk_size;
		region->stats.wr_chunks++;
		region->stats.wr_time.delta = timespec_add(
		        region->stats.wr_time.delta, t.delta);

		/* First time region was accessed */
		if (region->init_offset == offset)
			region->stats.wr_time.start = t.start;

		mutex_exit(&region->lock);
        }

	mutex_enter(&run_args->lock_ctl);
	run_args->threads_done++;
	mutex_exit(&run_args->lock_ctl);

	mutex_enter(&thr->lock);
	thr->rc = rc;
        thr->stats.wr_time.stop = current_kernel_time();
	mutex_exit(&thr->lock);
        wake_up(&run_args->waitq);

        set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();

	/* Check if we should exit */
	mutex_enter(&thr->lock);
	rc = thr->rc;
	mutex_exit(&thr->lock);
	if (rc)
		goto out;

	/* Read phase */
	mutex_enter(&thr->lock);
        thr->stats.rd_time.start = current_kernel_time();
	mutex_exit(&thr->lock);

        while (kpios_get_work_item(run_args, &obj, &offset,
				   &chunk_size, &region, DMU_READ)) {
		if (thread_delay) {
			get_random_bytes(&random_int, sizeof(unsigned int));
	                thread_delay_tmp = random_int % thread_delay;
		        set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(thread_delay_tmp); /* In jiffies */
		}

		if (run_args->flags & DMU_VERIFY)
			memset(buf, 0, chunk_size);

	        t.start = current_kernel_time();
		rc = kpios_dmu_read(run_args, obj.os, obj.obj,
				    offset, chunk_size, buf);
	        t.stop = current_kernel_time();
		t.delta = timespec_sub(t.stop, t.start);

		if (rc) {
			kpios_print(run_args->file, "IO error while doing "
				    "dmu_read(): %d\n", rc);
			break;
		}

		/* Trivial data verification, expensive! */
		if (run_args->flags & DMU_VERIFY) {
			for (i = 0; i < chunk_size; i++) {
				if (buf[i] != 'z') {
					kpios_print(run_args->file,
						    "IO verify error: %d/%d/%d\n",
					            (int)obj.obj, (int)offset,
					            (int)chunk_size);
					break;
				}
			}
		}

		mutex_enter(&thr->lock);
		thr->stats.rd_data += chunk_size;
		thr->stats.rd_chunks++;
		thr->stats.rd_time.delta = timespec_add(
		        thr->stats.rd_time.delta, t.delta);
		mutex_exit(&thr->lock);

		mutex_enter(&region->lock);
		region->stats.rd_data += chunk_size;
		region->stats.rd_chunks++;
		region->stats.rd_time.delta = timespec_add(
		        region->stats.rd_time.delta, t.delta);

		/* First time region was accessed */
		if (region->init_offset == offset)
			region->stats.rd_time.start = t.start;

		mutex_exit(&region->lock);
        }

	mutex_enter(&run_args->lock_ctl);
	run_args->threads_done++;
	mutex_exit(&run_args->lock_ctl);

	mutex_enter(&thr->lock);
	thr->rc = rc;
        thr->stats.rd_time.stop = current_kernel_time();
	mutex_exit(&thr->lock);
        wake_up(&run_args->waitq);

out:
	vmem_free(buf, chunk_size);
	do_exit(0);

	return rc; /* Unreachable, due to do_exit() */
}

static int
kpios_thread_done(run_args_t *run_args)
{
	ASSERT(run_args->threads_done <= run_args->thread_count);
        return (run_args->threads_done == run_args->thread_count);
}

static int
kpios_threads_run(run_args_t *run_args)
{
        struct task_struct *tsk, **tsks;
	thread_data_t *thr = NULL;
	kpios_time_t *tt = &(run_args->stats.total_time);
	kpios_time_t *tw = &(run_args->stats.wr_time);
	kpios_time_t *tr = &(run_args->stats.rd_time);
        int i, rc = 0, tc = run_args->thread_count;
        DEFINE_WAIT(wait);

	kpios_upcall(run_args->pre, PHASE_PRE, run_args, 0);

	tsks = kmem_zalloc(sizeof(struct task_struct *) * tc, KM_SLEEP);
	if (tsks == NULL) {
		rc = -ENOMEM;
		goto cleanup2;
	}

	run_args->threads = kmem_zalloc(sizeof(thread_data_t *) * tc, KM_SLEEP);
	if (run_args->threads == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	init_waitqueue_head(&run_args->waitq);
	run_args->threads_done = 0;

	/* Create all the needed threads which will sleep until awoken */
        for (i = 0; i < tc; i++) {
		thr = kmem_zalloc(sizeof(thread_data_t), KM_SLEEP);
		if (thr == NULL) {
			rc = -ENOMEM;
			goto taskerr;
		}

		thr->thread_no = i;
		thr->run_args = run_args;
		thr->rc = 0;
	        mutex_init(&thr->lock, NULL, MUTEX_DEFAULT, NULL);
		run_args->threads[i] = thr;

	        tsk = kthread_create(kpios_thread_main, (void *)thr,
	                             "%s/%d", "kpios_io", i);
	        if (IS_ERR(tsk)) {
			rc = -EINVAL;
			goto taskerr;
		}

		tsks[i] = tsk;
        }

        tt->start = current_kernel_time();

	/* Wake up all threads for write phase */
	kpios_upcall(run_args->pre, PHASE_WRITE, run_args, 0);
        for (i = 0; i < tc; i++)
		wake_up_process(tsks[i]);

	/* Wait for write phase to complete */
        tw->start = current_kernel_time();
        wait_event(run_args->waitq, kpios_thread_done(run_args));
        tw->stop = current_kernel_time();

        for (i = 0; i < tc; i++) {
		thr = run_args->threads[i];

		mutex_enter(&thr->lock);

		if (!rc && thr->rc)
			rc = thr->rc;

		run_args->stats.wr_data += thr->stats.wr_data;
		run_args->stats.wr_chunks += thr->stats.wr_chunks;
		mutex_exit(&thr->lock);
	}

	kpios_upcall(run_args->post, PHASE_WRITE, run_args, rc);
	if (rc) {
		/* Wake up all threads and tell them to exit */
		for (i = 0; i < tc; i++) {
			mutex_enter(&thr->lock);
			thr->rc = rc;
			mutex_exit(&thr->lock);

			wake_up_process(tsks[i]);
		}
		goto out;
	}

	mutex_enter(&run_args->lock_ctl);
	ASSERT(run_args->threads_done == run_args->thread_count);
	run_args->threads_done = 0;
	mutex_exit(&run_args->lock_ctl);

	/* Wake up all threads for read phase */
	kpios_upcall(run_args->pre, PHASE_READ, run_args, 0);
        for (i = 0; i < tc; i++)
		wake_up_process(tsks[i]);

	/* Wait for read phase to complete */
        tr->start = current_kernel_time();
        wait_event(run_args->waitq, kpios_thread_done(run_args));
        tr->stop = current_kernel_time();

        for (i = 0; i < tc; i++) {
		thr = run_args->threads[i];

		mutex_enter(&thr->lock);

		if (!rc && thr->rc)
			rc = thr->rc;

		run_args->stats.rd_data += thr->stats.rd_data;
		run_args->stats.rd_chunks += thr->stats.rd_chunks;
		mutex_exit(&thr->lock);
	}

	kpios_upcall(run_args->post, PHASE_READ, run_args, rc);
out:
        tt->stop = current_kernel_time();
	tt->delta = timespec_sub(tt->stop, tt->start);
	tw->delta = timespec_sub(tw->stop, tw->start);
	tr->delta = timespec_sub(tr->stop, tr->start);

cleanup:
	kmem_free(tsks, sizeof(struct task_struct *) * tc);
cleanup2:
	kpios_upcall(run_args->post, PHASE_POST, run_args, rc);

	/* Returns first encountered thread error (if any) */
	return rc;

taskerr:
	/* Destroy all threads that were created successfully */
	for (i = 0; i < tc; i++)
		if (tsks[i] != NULL)
			(void) kthread_stop(tsks[i]);

	goto cleanup;
}

static int
kpios_do_one_run(struct file *file, kpios_cmd_t *kcmd,
                 int data_size, void *data)
{
        run_args_t *run_args;
	kpios_stats_t *stats = (kpios_stats_t *)data;
	int i, n, m, size, rc;

	if ((!kcmd->cmd_chunk_size) || (!kcmd->cmd_region_size) ||
	    (!kcmd->cmd_thread_count) || (!kcmd->cmd_region_count)) {
		kpios_print(file, "Invalid chunk_size, region_size, "
			    "thread_count, or region_count, %d\n", -EINVAL);
		return -EINVAL;
	}

	if (!(kcmd->cmd_flags & DMU_WRITE) ||
	    !(kcmd->cmd_flags & DMU_READ)) {
		kpios_print(file, "Invalid flags, minimally DMU_WRITE "
			    "and DMU_READ must be set, %d\n", -EINVAL);
		return -EINVAL;
	}

	if ((kcmd->cmd_flags & (DMU_WRITE_ZC | DMU_READ_ZC)) &&
	    (kcmd->cmd_flags & DMU_VERIFY)) {
		kpios_print(file, "Invalid flags, DMU_*_ZC incompatible "
			    "with DMU_VERIFY, used for performance analysis "
			    "only, %d\n", -EINVAL);
		return -EINVAL;
	}

	/* Opaque data on return contains structs of the following form:
	 *
	 * kpios_stat_t stats[];
	 * stats[0]     = run_args->stats;
	 * stats[1-N]   = threads[N]->stats;
	 * stats[N+1-M] = regions[M]->stats;
	 *
	 * Where N is the number of threads, and M is the number of regions.
	 */
	size = (sizeof(kpios_stats_t) +
	       (kcmd->cmd_thread_count * sizeof(kpios_stats_t)) +
	       (kcmd->cmd_region_count * sizeof(kpios_stats_t)));
	if (data_size < size) {
		kpios_print(file, "Invalid size, command data buffer "
			    "size too small, (%d < %d)\n", data_size, size);
		return -ENOSPC;
	}

        rc = kpios_setup_run(&run_args, kcmd, file);
	if (rc)
		return rc;

        rc = kpios_threads_run(run_args);
	kpios_remove_objects(run_args);
	if (rc)
		goto cleanup;

	if (stats) {
		n = 1;
		m = 1 + kcmd->cmd_thread_count;
		stats[0] = run_args->stats;

		for (i = 0; i < kcmd->cmd_thread_count; i++)
			stats[n+i] = run_args->threads[i]->stats;

		for (i = 0; i < kcmd->cmd_region_count; i++)
			stats[m+i] = run_args->regions[i].stats;
	}

cleanup:
        kpios_cleanup_run(run_args);
	return rc;
}

static int
kpios_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	kpios_info_t *info;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	info = (kpios_info_t *)kmem_alloc(sizeof(*info), KM_SLEEP);
	if (info == NULL)
		return -ENOMEM;

	spin_lock_init(&info->info_lock);
	info->info_size = KPIOS_INFO_BUFFER_SIZE;
	info->info_buffer = (char *)vmem_alloc(KPIOS_INFO_BUFFER_SIZE,KM_SLEEP);
	if (info->info_buffer == NULL) {
		kmem_free(info, sizeof(*info));
		return -ENOMEM;
	}

	info->info_head = info->info_buffer;
	file->private_data = (void *)info;

        return 0;
}

static int
kpios_release(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	kpios_info_t *info = (kpios_info_t *)file->private_data;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	vmem_free(info->info_buffer, KPIOS_INFO_BUFFER_SIZE);
	kmem_free(info, sizeof(*info));

	return 0;
}

static int
kpios_buffer_clear(struct file *file, kpios_cfg_t *kcfg, unsigned long arg)
{
	kpios_info_t *info = (kpios_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	memset(info->info_buffer, 0, info->info_size);
	info->info_head = info->info_buffer;
	spin_unlock(&info->info_lock);

	return 0;
}

static int
kpios_buffer_size(struct file *file, kpios_cfg_t *kcfg, unsigned long arg)
{
	kpios_info_t *info = (kpios_info_t *)file->private_data;
	char *buf;
	int min, size, rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	if (kcfg->cfg_arg1 > 0) {

		size = kcfg->cfg_arg1;
		buf = (char *)vmem_alloc(size, KM_SLEEP);
		if (buf == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		/* Zero fill and truncate contents when coping buffer */
		min = ((size < info->info_size) ? size : info->info_size);
		memset(buf, 0, size);
		memcpy(buf, info->info_buffer, min);
		vmem_free(info->info_buffer, info->info_size);
		info->info_size = size;
		info->info_buffer = buf;
		info->info_head = info->info_buffer;
	}

	kcfg->cfg_rc1 = info->info_size;

	if (copy_to_user((struct kpios_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		rc = -EFAULT;
out:
	spin_unlock(&info->info_lock);

	return rc;
}

static int
kpios_ioctl_cfg(struct file *file, unsigned long arg)
{
	kpios_cfg_t kcfg;
	int rc = 0;

	if (copy_from_user(&kcfg, (kpios_cfg_t *)arg, sizeof(kcfg)))
		return -EFAULT;

	if (kcfg.cfg_magic != KPIOS_CFG_MAGIC) {
		kpios_print(file, "Bad config magic 0x%x != 0x%x\n",
		            kcfg.cfg_magic, KPIOS_CFG_MAGIC);
		return -EINVAL;
	}

	switch (kcfg.cfg_cmd) {
		case KPIOS_CFG_BUFFER_CLEAR:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Unused
			 */
			rc = kpios_buffer_clear(file, &kcfg, arg);
			break;
		case KPIOS_CFG_BUFFER_SIZE:
			/* cfg_arg1 - 0 - query size; >0 resize
			 * cfg_rc1  - Set to current buffer size
			 */
			rc = kpios_buffer_size(file, &kcfg, arg);
			break;
		default:
			kpios_print(file, "Bad config command %d\n",
				    kcfg.cfg_cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

static int
kpios_ioctl_cmd(struct file *file, unsigned long arg)
{
	kpios_cmd_t kcmd;
	int rc = -EINVAL;
	void *data = NULL;

	rc = copy_from_user(&kcmd, (kpios_cfg_t *)arg, sizeof(kcmd));
	if (rc) {
		kpios_print(file, "Unable to copy command structure "
			    "from user to kernel memory, %d\n", rc);
		return -EFAULT;
	}

	if (kcmd.cmd_magic != KPIOS_CMD_MAGIC) {
		kpios_print(file, "Bad command magic 0x%x != 0x%x\n",
		            kcmd.cmd_magic, KPIOS_CFG_MAGIC);
		return -EINVAL;
	}

	/* Allocate memory for any opaque data the caller needed to pass on */
	if (kcmd.cmd_data_size > 0) {
		data = (void *)vmem_alloc(kcmd.cmd_data_size, KM_SLEEP);
		if (data == NULL) {
			kpios_print(file, "Unable to vmem_alloc() %ld "
				    "bytes for data buffer\n",
				    (long)kcmd.cmd_data_size);
			return -ENOMEM;
		}

		rc = copy_from_user(data, (void *)(arg + offsetof(kpios_cmd_t,
		                    cmd_data_str)), kcmd.cmd_data_size);
		if (rc) {
			kpios_print(file, "Unable to copy data buffer "
				    "from user to kernel memory, %d\n", rc);
			vmem_free(data, kcmd.cmd_data_size);
			return -EFAULT;
		}
	}

	rc = kpios_do_one_run(file, &kcmd, kcmd.cmd_data_size, data);

	if (data != NULL) {
		/* If the test failed do not print out the stats */
		if (rc)
			goto cleanup;

		rc = copy_to_user((void *)(arg + offsetof(kpios_cmd_t,
		                  cmd_data_str)), data, kcmd.cmd_data_size);
		if (rc) {
			kpios_print(file, "Unable to copy data buffer "
				    "from kernel to user memory, %d\n", rc);
			rc = -EFAULT;
		}

cleanup:
		vmem_free(data, kcmd.cmd_data_size);
	}

	return rc;
}

static int
kpios_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, unsigned long arg)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	int rc = 0;

	/* Ignore tty ioctls */
	if ((cmd & 0xffffff00) == ((int)'T') << 8)
		return -ENOTTY;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	switch (cmd) {
		case KPIOS_CFG:
			rc = kpios_ioctl_cfg(file, arg);
			break;
		case KPIOS_CMD:
			rc = kpios_ioctl_cmd(file, arg);
			break;
		default:
			kpios_print(file, "Bad ioctl command %d\n", cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

/* I'm not sure why you would want to write in to this buffer from
 * user space since its principle use is to pass test status info
 * back to the user space, but I don't see any reason to prevent it.
 */
static ssize_t
kpios_write(struct file *file, const char __user *buf,
            size_t count, loff_t *ppos)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kpios_info_t *info = (kpios_info_t *)file->private_data;
	int rc = 0;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Write beyond EOF */
	if (*ppos >= info->info_size) {
		rc = -EFBIG;
		goto out;
	}

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_from_user(info->info_buffer, buf, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return rc;
}

static ssize_t
kpios_read(struct file *file, char __user *buf,
		        size_t count, loff_t *ppos)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kpios_info_t *info = (kpios_info_t *)file->private_data;
	int rc = 0;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Read beyond EOF */
	if (*ppos >= info->info_size)
		goto out;

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_to_user(buf, info->info_buffer + *ppos, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return rc;
}

static loff_t kpios_seek(struct file *file, loff_t offset, int origin)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kpios_info_t *info = (kpios_info_t *)file->private_data;
	int rc = -EINVAL;

	if (minor >= KPIOS_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	switch (origin) {
	case 0: /* SEEK_SET - No-op just do it */
		break;
	case 1: /* SEEK_CUR - Seek from current */
		offset = file->f_pos + offset;
		break;
	case 2: /* SEEK_END - Seek from end */
		offset = info->info_size + offset;
		break;
	}

	if (offset >= 0) {
		file->f_pos = offset;
		file->f_version = 0;
		rc = offset;
	}

	spin_unlock(&info->info_lock);

	return rc;
}

static struct file_operations kpios_fops = {
	.owner   = THIS_MODULE,
	.open    = kpios_open,
	.release = kpios_release,
	.ioctl   = kpios_ioctl,
	.read    = kpios_read,
	.write   = kpios_write,
	.llseek  = kpios_seek,
};

static struct cdev kpios_cdev = {
	.owner  =       THIS_MODULE,
	.kobj   =       { .name = "kpios", },
};

static int __init
kpios_init(void)
{
	dev_t dev;
	int rc;

	dev = MKDEV(KPIOS_MAJOR, 0);
	if ((rc = register_chrdev_region(dev, KPIOS_MINORS, "kpios")))
		goto error;

	/* Support for registering a character driver */
	cdev_init(&kpios_cdev, &kpios_fops);
	if ((rc = cdev_add(&kpios_cdev, dev, KPIOS_MINORS))) {
		printk(KERN_ERR "kpios: Error adding cdev, %d\n", rc);
		kobject_put(&kpios_cdev.kobj);
		unregister_chrdev_region(dev, KPIOS_MINORS);
		goto error;
	}

	/* Support for udev make driver info available in sysfs */
	kpios_class = class_create(THIS_MODULE, "kpios");
	if (IS_ERR(kpios_class)) {
		rc = PTR_ERR(kpios_class);
		printk(KERN_ERR "kpios: Error creating kpios class, %d\n", rc);
		cdev_del(&kpios_cdev);
		unregister_chrdev_region(dev, KPIOS_MINORS);
		goto error;
	}

	class_device_create(kpios_class, NULL, dev, NULL, "kpios");

	printk(KERN_INFO "kpios: Loaded Kernel PIOS Tests v%s\n", VERSION);
	return 0;
error:
	printk(KERN_ERR "kpios: Error registering kpios device, %d\n", rc);
	return rc;
}

static void
kpios_fini(void)
{
	dev_t dev = MKDEV(KPIOS_MAJOR, 0);

	class_device_destroy(kpios_class, dev);
	class_destroy(kpios_class);

	cdev_del(&kpios_cdev);
	unregister_chrdev_region(dev, KPIOS_MINORS);

	printk(KERN_INFO "kpios: Unloaded Kernel PIOS Tests\n");
	return;
}

module_init(kpios_init);
module_exit(kpios_fini);

MODULE_AUTHOR("LLNL / Sun");
MODULE_DESCRIPTION("Kernel PIOS implementation");
MODULE_LICENSE("GPL");
