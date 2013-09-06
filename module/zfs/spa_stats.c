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

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>

/*
 * Keeps stats on last N reads per spa_t, disabled by default.
 */
int zfs_read_history = 0;

/*
 * ==========================================================================
 * SPA Read History Routines
 * ==========================================================================
 */

/*
 * Read statistics - Information exported regarding each arc_read call
 */
typedef struct kstat_read {
	uint64_t	uid;		/* unique identifier */
	hrtime_t	start;		/* time read completed */
	uint64_t	objset;		/* read from this objset */
	uint64_t	object;		/* read of this object number */
	uint64_t	level;		/* block's indirection level */
	uint64_t	blkid;		/* read of this block id */
	char		origin[24];	/* read originated from here */
	uint32_t	aflags;		/* ARC flags (cached, prefetch, etc.) */
	pid_t		pid;		/* PID of task doing read */
	char		comm[16];	/* command name of task doing read */
} kstat_read_t;

typedef struct spa_read_history {
	kstat_read_t	srh_kstat;
	list_node_t	srh_link;
} spa_read_history_t;

void
spa_read_history_headers(char *buf, size_t size)
{
	snprintf(buf, size, "%-8s %-18s %-8s %-8s %-8s %-8s %-10s "
		 "%-24s %-8s %-16s\n", "UID", "start", "objset", "object",
		 "level", "blkid", "aflags", "origin", "pid", "command");
	buf[size-1] = '\0';
}

void
spa_read_history_data(char *buf, size_t size, void *data)
{
	kstat_read_t *krp = (kstat_read_t *)data;

	snprintf(buf, size, "%-8llu %-18llu 0x%-6llx %-8lli %-8lli %-8lli "
		 "0x%-6x %-24s %-8i %-16s\n", (u_longlong_t)krp->uid, krp->start,
		 (longlong_t)krp->objset, (longlong_t)krp->object,
		 (longlong_t)krp->level, (longlong_t)krp->blkid,
		 krp->aflags, krp->origin, krp->pid, krp->comm);
	buf[size-1] = '\0';
}

void *
spa_read_history_addr(kstat_t *ksp, loff_t index)
{
	return ksp->ks_data + index * sizeof(kstat_read_t);
}

static int
spa_read_history_update(kstat_t *ksp, int rw)
{
	spa_t *spa = ksp->ks_private;
	spa_read_history_t *srh;
	int i = 0;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	if (ksp->ks_data) {
		vmem_free(ksp->ks_data, ksp->ks_data_size);
		ksp->ks_data = NULL;
	}

	mutex_enter(&spa->spa_read_history_lock);

	ksp->ks_ndata = spa->spa_read_history_size;
	ksp->ks_data_size = spa->spa_read_history_size * sizeof(kstat_read_t);

	if (ksp->ks_data_size > 0)
		ksp->ks_data = vmem_alloc(ksp->ks_data_size, KM_PUSHPAGE);

	for (srh = list_tail(&spa->spa_read_history); srh != NULL;
	     srh = list_prev(&spa->spa_read_history, srh)) {
		ASSERT3S(i + sizeof(kstat_read_t), <=, ksp->ks_data_size);
		memcpy(ksp->ks_data + i, &srh->srh_kstat, sizeof(kstat_read_t));
		i += sizeof(kstat_read_t);
	}

	mutex_exit(&spa->spa_read_history_lock);

	return (0);
}

void
spa_read_history_init(spa_t *spa)
{
	char name[KSTAT_STRLEN];
	kstat_t *ksp;

	mutex_init(&spa->spa_read_history_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&spa->spa_read_history, sizeof (spa_read_history_t),
	    offsetof(spa_read_history_t, srh_link));

	spa->spa_read_history_count = 0;
	spa->spa_read_history_size = 0;

	(void) snprintf(name, KSTAT_STRLEN, "reads-%s", spa_name(spa));
	name[KSTAT_STRLEN-1] = '\0';

	ksp = kstat_create("zfs", 0, name, "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	spa->spa_read_history_kstat = ksp;

	if (ksp) {
		ksp->ks_data = NULL;
		ksp->ks_private = spa;
		ksp->ks_update = spa_read_history_update;
		kstat_set_raw_ops(ksp, spa_read_history_headers,
				       spa_read_history_data,
				       spa_read_history_addr);
		kstat_install(ksp);
	}
}

void
spa_read_history_destroy(spa_t *spa)
{
	spa_read_history_t *srh;
	kstat_t *ksp;

	ksp = spa->spa_read_history_kstat;
	if (ksp) {
		if (ksp->ks_data)
			vmem_free(ksp->ks_data, ksp->ks_data_size);

		kstat_delete(ksp);
	}

	mutex_enter(&spa->spa_read_history_lock);
	while ((srh = list_remove_head(&spa->spa_read_history))) {
		spa->spa_read_history_size--;
		kmem_free(srh, sizeof(spa_read_history_t));
	}

	ASSERT3U(spa->spa_read_history_size, ==, 0);
	list_destroy(&spa->spa_read_history);
	mutex_exit(&spa->spa_read_history_lock);

	mutex_destroy(&spa->spa_read_history_lock);
}

void
spa_read_history_add(spa_t *spa, const zbookmark_t *zb, uint32_t aflags)
{
	spa_read_history_t *srh, *rm;
	unsigned int size;

	ASSERT3P(spa, !=, NULL);
	ASSERT3P(zb,  !=, NULL);

	/* Must also check history_size in case we need to clear the list */
	if (zfs_read_history == 0 && spa->spa_read_history_size == 0)
		return;

	srh = kmem_zalloc(sizeof(spa_read_history_t), KM_PUSHPAGE);

	size = sizeof(srh->srh_kstat.origin);
	strncpy(srh->srh_kstat.origin, zb->zb_func, size);
	srh->srh_kstat.origin[size-1] = '\0';

	srh->srh_kstat.start  = gethrtime();
	srh->srh_kstat.objset = zb->zb_objset;
	srh->srh_kstat.object = zb->zb_object;
	srh->srh_kstat.level  = zb->zb_level;
	srh->srh_kstat.blkid  = zb->zb_blkid;
	srh->srh_kstat.aflags = aflags;

	srh->srh_kstat.pid    = getpid();
	size = sizeof(srh->srh_kstat.comm);
	strncpy(srh->srh_kstat.comm, getcomm(), size);
	srh->srh_kstat.comm[size-1] = '\0';

	mutex_enter(&spa->spa_read_history_lock);

	srh->srh_kstat.uid = spa->spa_read_history_count++;

	list_insert_head(&spa->spa_read_history, srh);
	spa->spa_read_history_size++;

	while (spa->spa_read_history_size > zfs_read_history) {
		spa->spa_read_history_size--;
		rm = list_remove_tail(&spa->spa_read_history);
		kmem_free(rm, sizeof(spa_read_history_t));
	}

	mutex_exit(&spa->spa_read_history_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_read_history, int, 0644);
MODULE_PARM_DESC(zfs_read_history, "Historic statistics for the last N reads");
#endif
