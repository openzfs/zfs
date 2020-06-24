/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/spa.h>
#include <zfs_comutil.h>

void
spa_stats_init(spa_t *spa)
{

}

void
spa_stats_destroy(spa_t *spa)
{

}

void
spa_iostats_trim_add(spa_t *spa, trim_type_t type,
    uint64_t extents_written, uint64_t bytes_written,
    uint64_t extents_skipped, uint64_t bytes_skipped,
    uint64_t extents_failed, uint64_t bytes_failed)
{
}

void
spa_read_history_add(spa_t *spa, const zbookmark_phys_t *zb, uint32_t aflags)
{
}

void
spa_txg_history_add(spa_t *spa, uint64_t txg, hrtime_t birth_time)
{

}
/*
 * Set txg state completion time and increment current state.
 */
int
spa_txg_history_set(spa_t *spa, uint64_t txg, txg_state_t completed_state,
    hrtime_t completed_time)
{
	return (0);
}

txg_stat_t *
spa_txg_history_init_io(spa_t *spa, uint64_t txg, dsl_pool_t *dp)
{
	return (NULL);
}

void
spa_txg_history_fini_io(spa_t *spa, txg_stat_t *ts)
{

}

void
spa_tx_assign_add_nsecs(spa_t *spa, uint64_t nsecs)
{

}

void
spa_mmp_history_add(spa_t *spa, uint64_t txg, uint64_t timestamp,
    uint64_t mmp_delay, vdev_t *vd, int label, uint64_t mmp_node_id,
    int error)
{

}

int
spa_mmp_history_set(spa_t *spa, uint64_t mmp_node_id, int io_error,
    hrtime_t duration)
{
	return (0);
}

int
spa_mmp_history_set_skip(spa_t *spa, uint64_t mmp_node_id)
{
	return (0);
}
