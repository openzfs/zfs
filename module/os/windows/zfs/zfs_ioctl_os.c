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
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/fm/util.h>
#include <sys/dsl_crypt.h>
#include <sys/zfs_windows.h>

#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/zvol_os.h>

#include <zfs_gitrev.h>

#include <Wdmsec.h>



typedef struct {
    unsigned __int64	read_iops;
    unsigned __int64	write_iops;
    unsigned __int64	total_iops;
    unsigned __int64	read_bytes;
    unsigned __int64	write_bytes;
    unsigned __int64	total_bytes;
    unsigned __int64	ddt_entry_count; // number of elments in ddt ,zpool only
    unsigned __int64	ddt_dspace; // size of ddt on disk   ,zpool only
    unsigned __int64	ddt_mspace; // size of ddt in-core   ,zpool only
    unsigned __int64	vsx_active_queue_sync_read;
    unsigned __int64	vsx_active_queue_sync_write;
    unsigned __int64	vsx_active_queue_async_read;
    unsigned __int64	vsx_active_queue_async_write;
    unsigned __int64	vsx_pend_queue_sync_read;
    unsigned __int64	vsx_pend_queue_sync_write;
    unsigned __int64	vsx_pend_queue_async_read;
    unsigned __int64	vsx_pend_queue_async_write;
    unsigned __int64	vsx_queue_histo_sync_read_time;
    unsigned __int64	vsx_queue_histo_sync_read_count;
    unsigned __int64	vsx_queue_histo_async_read_time;
    unsigned __int64	vsx_queue_histo_async_read_count;
    unsigned __int64	vsx_queue_histo_sync_write_time;
    unsigned __int64	vsx_queue_histo_sync_write_count;
    unsigned __int64	vsx_queue_histo_async_write_time;
    unsigned __int64	vsx_queue_histo_async_write_count;
    unsigned __int64	vsx_total_histo_read_time;
    unsigned __int64	vsx_total_histo_read_count;
    unsigned __int64	vsx_total_histo_write_time;
    unsigned __int64	vsx_total_histo_write_count;
    unsigned __int64	vsx_disk_histo_read_time;
    unsigned __int64	vsx_disk_histo_read_count;
    unsigned __int64	vsx_disk_histo_write_time;
    unsigned __int64	vsx_disk_histo_write_count;
    unsigned __int64	dp_dirty_total_io;	// zpool only
} zpool_perf_counters;

NTSTATUS NTAPI
ZFSinPerfCallBack(PCW_CALLBACK_TYPE Type, PPCW_CALLBACK_INFORMATION Info,
    PVOID Context);

void ZFSinPerfCollect(PCW_MASK_INFORMATION CollectData);
void ZFSinPerfVdevCollect(PCW_MASK_INFORMATION CollectData);
void ZFSinCachePerfCollect(PCW_MASK_INFORMATION CollectData);

PUNICODE_STRING MapInvalidChars(PUNICODE_STRING InstanceName);

void ZFSinPerfEnumerate(PCW_MASK_INFORMATION EnumerateInstances);
void ZFSinPerfVdevEnumerate(PCW_MASK_INFORMATION EnumerateInstances);
void ZFSinCachePerfEnumerate(PCW_MASK_INFORMATION EnumerateInstances);

#include <sys/spa_impl.h>
#include <sys/zfs_ioctl.h>
#include "../OpenZFS_perf.h"
#include "../OpenZFS_counters.h"
#include <sys/vdev_impl.h>

// extern void zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject);

int zfs_major			= 0;
int zfs_bmajor			= 0;
static void *zfs_devnode 	= NULL;
#define	ZFS_MAJOR		-24

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	int error = 0;

	if (*zfvp == NULL || (*zfvp)->z_vfs == NULL)
		return (SET_ERROR(ESRCH));

	error = vfs_busy((*zfvp)->z_vfs, LK_NOWAIT);
	if (error != 0) {
		*zfvp = NULL;
		error = SET_ERROR(ESRCH);
	}
	return (error);
}

NTSTATUS NTAPI
ZFSinPerfCallBack(PCW_CALLBACK_TYPE Type, PPCW_CALLBACK_INFORMATION Info,
    PVOID Context)
{
	UNREFERENCED_PARAMETER(Context);

	switch (Type) {
	case PcwCallbackEnumerateInstances:
	{
		ZFSinPerfEnumerate(Info->EnumerateInstances);
		break;
	}
	case PcwCallbackCollectData:
	{
		ZFSinPerfCollect(Info->CollectData);

		break;
	}
	default: break;
	}

	return (STATUS_SUCCESS);
}

NTSTATUS NTAPI
ZFSinPerfVdevCallBack(PCW_CALLBACK_TYPE Type, PPCW_CALLBACK_INFORMATION Info,
    PVOID Context)
{
	UNREFERENCED_PARAMETER(Context);

	switch (Type) {
	case PcwCallbackEnumerateInstances:
	{
		ZFSinPerfVdevEnumerate(Info->EnumerateInstances);

		break;
	}
	case PcwCallbackCollectData:
	{
		ZFSinPerfVdevCollect(Info->CollectData);

		break;
	}
	default: break;
	}

	return (STATUS_SUCCESS);
}

NTSTATUS NTAPI
ZFSinCachePerfCallBack(PCW_CALLBACK_TYPE Type, PPCW_CALLBACK_INFORMATION
    Info, PVOID Context)
{
	UNREFERENCED_PARAMETER(Context);

	switch (Type) {
	case PcwCallbackEnumerateInstances:
	{
		ZFSinCachePerfEnumerate(Info->EnumerateInstances);

		break;
	}
	case PcwCallbackCollectData:
	{
		ZFSinCachePerfCollect(Info->CollectData);

		break;
	}
	default: break;
	}

	return (STATUS_SUCCESS);

}




PUNICODE_STRING
MapInvalidChars(PUNICODE_STRING InstanceName)
{
	const WCHAR wInvalidChars[] = L"()#\\/";
	const WCHAR wMappedChars[] = L"[]___";
	const LONG lArraySize = ARRAY_SIZE(wInvalidChars) - 1;

	for (LONG i = 0; i < InstanceName->Length / sizeof (WCHAR); ++i) {
		for (LONG j = 0; j < lArraySize; ++j) {
			if (InstanceName->Buffer[i] == wInvalidChars[j]) {
				InstanceName->Buffer[i] = wMappedChars[j];
				break;
			}
		}
	}
	return (InstanceName);
}

void
ZFSinPerfVdevEnumerate(PCW_MASK_INFORMATION EnumerateInstances)
{
	spa_t *spa_perf = NULL;
	NTSTATUS status;
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;
	mutex_enter(&spa_namespace_lock);
	while ((spa_perf = spa_next(spa_perf)) != NULL) {
		vdev_t *vd = spa_perf->spa_root_vdev;
		char vdev_zpool[ZFS_MAX_DATASET_NAME_LEN] = { 0 };

		for (int c = 0; c < vd->vdev_children; c++) {
		char *vdev_name = vd->vdev_child[c]->vdev_path;
		if (!vdev_name || !vdev_name[0])
			continue;

			snprintf(vdev_zpool, ZFS_MAX_DATASET_NAME_LEN, "%s_%s",
			    vdev_name + 5, spa_perf->spa_name);
			/* Neglecting first five characters of vdev_name */

			ANSI_STRING ansi_vdev;
			RtlInitAnsiString(&ansi_vdev, vdev_zpool);
			status = RtlAnsiStringToUnicodeString(&unicodeName,
			    &ansi_vdev, FALSE);

			if (!NT_SUCCESS(status)) {
				TraceEvent(TRACE_ERROR,
		    "%s:%d: Ansi to Unicode string conversion failed for %Z\n",
				    __func__, __LINE__, &ansi_vdev);
				continue;
			}

			status = AddZFSinPerfVdev(EnumerateInstances.Buffer,
			    MapInvalidChars(&unicodeName), 0, NULL);
			if (!NT_SUCCESS(status)) {
				TraceEvent(TRACE_ERROR,
			    "%s:%d: AddZFSinPerfVdev failed - status 0x%x\n",
				    __func__, __LINE__, status);
			}
		}
	}
	mutex_exit(&spa_namespace_lock);
	UNICODE_STRING total;
	RtlInitUnicodeString(&total, L"_Total");
	status = AddZFSinPerfVdev(EnumerateInstances.Buffer,
	    MapInvalidChars(&total), 0, NULL);
	if (!NT_SUCCESS(status)) {
		TraceEvent(TRACE_ERROR,
		    "%s:%d: AddZFSinPerfVdev failed - status 0x%x\n",
		    __func__, __LINE__, status);
	}
	kmem_free(unicodeName.Buffer, sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN);
}


void ZFSinPerfEnumerate(PCW_MASK_INFORMATION EnumerateInstances) {
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;

	spa_t *spa_perf = NULL;
	ANSI_STRING ansi_spa;

	mutex_enter(&spa_namespace_lock);
	while ((spa_perf = spa_next(spa_perf)) != NULL) {
		spa_open_ref(spa_perf, FTAG);
		RtlInitAnsiString(&ansi_spa, spa_perf->spa_name);
		spa_close(spa_perf, FTAG);

		status = RtlAnsiStringToUnicodeString(&unicodeName, &ansi_spa,
		    FALSE);
		if (!NT_SUCCESS(status)) {
			TraceEvent(TRACE_ERROR,
		    "%s:%d: Ansi to Unicode string conversion failed for %Z\n",
			    __func__, __LINE__, &ansi_spa);
			continue;
		}

		status = AddZFSinPerf(EnumerateInstances.Buffer,
		    MapInvalidChars(&unicodeName), 0, NULL);
		if (!NT_SUCCESS(status)) {
			TraceEvent(TRACE_ERROR,
			    "%s:%d: AddZFSinPerf failed - status 0x%x\n",
			    __func__, __LINE__, status);
		}
	}
	mutex_exit(&spa_namespace_lock);

	UNICODE_STRING total;
	RtlInitUnicodeString(&total, L"_Total");
	status = AddZFSinPerf(EnumerateInstances.Buffer,
	    MapInvalidChars(&total), 0, NULL);
	if (!NT_SUCCESS(status)) {
		TraceEvent(TRACE_ERROR,
		    "%s:%d: AddZFSinPerf failed - status 0x%x\n",
		    __func__, __LINE__, status);
	}

	kmem_free(unicodeName.Buffer, sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN);
}

void ZFSinCachePerfEnumerate(PCW_MASK_INFORMATION EnumerateInstances) {
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;

	ANSI_STRING ansi_spa;
	RtlInitAnsiString(&ansi_spa, "Total");

	NTSTATUS status = RtlAnsiStringToUnicodeString(
	    &unicodeName, &ansi_spa, FALSE);
	if (!NT_SUCCESS(status))
	{
		TraceEvent(TRACE_ERROR,
		    "%s:%d: Ansi to Unicode string conversion failed for %Z\n",
		    __func__, __LINE__, &ansi_spa);
	}
	else
	{
		status = AddZFSinCachePerf(EnumerateInstances.Buffer,
		    MapInvalidChars(&unicodeName), 0, NULL);
		if (!NT_SUCCESS(status)) {
			TraceEvent(TRACE_ERROR,
			    "%s:%d: AddZFSinCachePerf failed - status 0x%x\n",
			    __func__, __LINE__, status);
		}
	}
	kmem_free(unicodeName.Buffer, sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN);
}

void
latency_stats(uint64_t *histo, unsigned int buckets, stat_pair* lat)
{
	int i;
	lat->count = 0;
	lat->total = 0;

	for (i = 0; i < buckets; i++) {
		/*
		 * Our buckets are power-of-two latency ranges.  Use the
		 * midpoint latency of each bucket to calculate the average.
		 * For example:
		 *
		 * Bucket          Midpoint
		 * 8ns-15ns:       12ns
		 * 16ns-31ns:      24ns
		 * ...
		 */
		if (histo[i] != 0) {
			lat->total += histo[i] *
			    (((1UL << i) + ((1UL << i) / 2)));
			lat->count += histo[i];
		}
	}
}

void
update_perf(vdev_stat_ex_t *vsx, vdev_stat_t *vs, ddt_object_t *ddo,
    dsl_pool_t *spad, zpool_perf_counters *perf)
{
	if (!perf)
		return;

	if (ddo) {
		perf->ddt_entry_count = ddo->ddo_count;
		perf->ddt_dspace = ddo->ddo_dspace * ddo->ddo_count;
		perf->ddt_mspace = ddo->ddo_mspace * ddo->ddo_count;
	}

	if (vs) {
		perf->read_iops = vs->vs_ops[ZIO_TYPE_READ];
		perf->write_iops = vs->vs_ops[ZIO_TYPE_WRITE];
		perf->read_bytes = vs->vs_bytes[ZIO_TYPE_READ];
		perf->write_bytes = vs->vs_bytes[ZIO_TYPE_WRITE];
		perf->total_bytes = vs->vs_bytes[ZIO_TYPE_WRITE] +
		    vs->vs_bytes[ZIO_TYPE_READ];
		perf->total_iops = vs->vs_ops[ZIO_TYPE_WRITE] +
		    vs->vs_ops[ZIO_TYPE_READ];
	}

	if (vsx) {
		perf->vsx_active_queue_sync_read =
		    vsx->vsx_active_queue[ZIO_PRIORITY_SYNC_READ];
		perf->vsx_active_queue_sync_write =
		    vsx->vsx_active_queue[ZIO_PRIORITY_SYNC_WRITE];
		perf->vsx_active_queue_async_read =
		    vsx->vsx_active_queue[ZIO_PRIORITY_ASYNC_READ];
		perf->vsx_active_queue_async_write =
		    vsx->vsx_active_queue[ZIO_PRIORITY_ASYNC_WRITE];
		perf->vsx_pend_queue_sync_read =
		    vsx->vsx_pend_queue[ZIO_PRIORITY_SYNC_READ];
		perf->vsx_pend_queue_sync_write =
		    vsx->vsx_pend_queue[ZIO_PRIORITY_SYNC_WRITE];
		perf->vsx_pend_queue_async_read =
		    vsx->vsx_pend_queue[ZIO_PRIORITY_ASYNC_READ];
		perf->vsx_pend_queue_async_write =
		    vsx->vsx_pend_queue[ZIO_PRIORITY_ASYNC_WRITE];


		stat_pair lat;
		latency_stats(&vsx->vsx_queue_histo[ZIO_PRIORITY_SYNC_READ][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_queue_histo_sync_read_time = lat.total;
		perf->vsx_queue_histo_sync_read_count = lat.count;

		latency_stats(&vsx->vsx_queue_histo[ZIO_PRIORITY_SYNC_WRITE][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_queue_histo_sync_write_time = lat.total;
		perf->vsx_queue_histo_sync_write_count = lat.count;

		latency_stats(&vsx->vsx_queue_histo[ZIO_PRIORITY_ASYNC_READ][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_queue_histo_async_read_time = lat.total;
		perf->vsx_queue_histo_async_read_count = lat.count;

		latency_stats(&vsx->vsx_queue_histo[ZIO_PRIORITY_ASYNC_WRITE]
		    [0], VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_queue_histo_async_write_time = lat.total;
		perf->vsx_queue_histo_async_write_count = lat.count;

		latency_stats(&vsx->vsx_total_histo[ZIO_TYPE_READ][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_total_histo_read_time = lat.total;
		perf->vsx_total_histo_read_count = lat.count;

		latency_stats(&vsx->vsx_total_histo[ZIO_TYPE_WRITE][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_total_histo_write_time = lat.total;
		perf->vsx_total_histo_write_count = lat.count;

		latency_stats(&vsx->vsx_disk_histo[ZIO_TYPE_READ][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_disk_histo_read_time = lat.total;
		perf->vsx_disk_histo_read_count = lat.count;

		latency_stats(&vsx->vsx_disk_histo[ZIO_TYPE_WRITE][0],
		    VDEV_L_HISTO_BUCKETS, &lat);
		perf->vsx_disk_histo_write_time = lat.total;
		perf->vsx_disk_histo_write_count = lat.count;
	}

	if (spad)
		perf->dp_dirty_total_io = spad->dp_dirty_total;
}


void
update_total_perf(zpool_perf_counters* perf, zpool_perf_counters* total_perf)
{
	total_perf->ddt_entry_count += perf->ddt_entry_count;
	total_perf->ddt_dspace += perf->ddt_dspace;
	total_perf->ddt_mspace += perf->ddt_mspace;
	total_perf->read_iops += perf->read_iops;
	total_perf->write_iops += perf->write_iops;
	total_perf->read_bytes += perf->read_bytes;
	total_perf->write_bytes += perf->write_bytes;
	total_perf->total_iops += (perf->read_iops + perf->write_iops);
	total_perf->total_bytes += (perf->read_bytes + perf->write_bytes);
	total_perf->vsx_active_queue_sync_read +=
	    perf->vsx_active_queue_sync_read;
	total_perf->vsx_active_queue_sync_write +=
	    perf->vsx_active_queue_sync_write;
	total_perf->vsx_active_queue_async_read +=
	    perf->vsx_active_queue_async_read;
	total_perf->vsx_active_queue_async_write +=
	    perf->vsx_active_queue_async_write;
	total_perf->vsx_pend_queue_sync_read +=
	    perf->vsx_pend_queue_sync_read;
	total_perf->vsx_pend_queue_sync_write +=
	    perf->vsx_pend_queue_sync_write;
	total_perf->vsx_pend_queue_async_read +=
	    perf->vsx_pend_queue_async_read;
	total_perf->vsx_pend_queue_async_write +=
	    perf->vsx_pend_queue_async_write;
	total_perf->vsx_disk_histo_read_time +=
	    perf->vsx_disk_histo_read_time;
	total_perf->vsx_disk_histo_read_count +=
	    perf->vsx_disk_histo_read_count;
	total_perf->vsx_disk_histo_write_time +=
	    perf->vsx_disk_histo_write_time;
	total_perf->vsx_disk_histo_write_count +=
	    perf->vsx_disk_histo_write_count;
	total_perf->vsx_total_histo_read_time +=
	    perf->vsx_total_histo_read_time;
	total_perf->vsx_total_histo_read_count +=
	    perf->vsx_total_histo_read_count;
	total_perf->vsx_total_histo_write_time +=
	    perf->vsx_total_histo_write_time;
	total_perf->vsx_total_histo_write_count +=
	    perf->vsx_total_histo_write_count;
	total_perf->vsx_queue_histo_sync_read_time +=
	    perf->vsx_queue_histo_sync_read_time;
	total_perf->vsx_queue_histo_sync_read_count +=
	    perf->vsx_queue_histo_sync_read_count;
	total_perf->vsx_queue_histo_sync_write_time +=
	    perf->vsx_queue_histo_sync_write_time;
	total_perf->vsx_queue_histo_sync_write_count +=
	    perf->vsx_queue_histo_sync_write_count;
	total_perf->vsx_queue_histo_async_read_time +=
	    perf->vsx_queue_histo_async_read_time;
	total_perf->vsx_queue_histo_async_read_count +=
	    perf->vsx_queue_histo_async_read_count;
	total_perf->vsx_queue_histo_async_write_time +=
	    perf->vsx_queue_histo_async_write_time;
	total_perf->vsx_queue_histo_async_write_count +=
	    perf->vsx_queue_histo_async_write_count;
	total_perf->dp_dirty_total_io += perf->dp_dirty_total_io;
}


void ZFSinPerfCollect(PCW_MASK_INFORMATION CollectData) {
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;

	ANSI_STRING ansi_spa;
	spa_t *spa_perf = NULL;
	zpool_perf_counters total_perf = { 0 };

	mutex_enter(&spa_namespace_lock);
	while ((spa_perf = spa_next(spa_perf)) != NULL) {
		zpool_perf_counters perf = { 0 };
		spa_open_ref(spa_perf, FTAG);
		RtlInitAnsiString(&ansi_spa, spa_perf->spa_name);
		ddt_object_t ddo = { 0 };
		vdev_stat_t vs = { 0 };
		vdev_stat_ex_t vsx = { 0 };

		spa_config_enter(spa_perf, SCL_ALL, FTAG, RW_READER);
		vdev_get_stats_ex(spa_perf->spa_root_vdev, &vs, &vsx);
		ddt_get_dedup_object_stats(spa_perf, &ddo);
		dsl_pool_t *spad = spa_get_dsl(spa_perf);

		update_perf(&vsx, &vs, &ddo, spad, &perf);
		spa_config_exit(spa_perf, SCL_ALL, FTAG);
		spa_close(spa_perf, FTAG);

		update_total_perf(&perf, &total_perf);

		status = RtlAnsiStringToUnicodeString(&unicodeName, &ansi_spa,
		    FALSE);
		if (!NT_SUCCESS(status)) {
			TraceEvent(TRACE_ERROR,
		    "%s:%d: Ansi to Unicode string conversion failed for %Z\n",
			    __func__, __LINE__, &ansi_spa);
			continue;
		}

		status = AddZFSinPerf(CollectData.Buffer,
		    MapInvalidChars(&unicodeName),
		    0, &perf);

		if (!NT_SUCCESS(status)) {
		    TraceEvent(TRACE_ERROR,
			"%s:%d: AddZFSinPerf failed - status 0x%x\n",
			__func__, __LINE__, status);
		}
	}
	mutex_exit(&spa_namespace_lock);

	UNICODE_STRING total;
	RtlInitUnicodeString(&total, L"_Total");
	status = AddZFSinPerf(CollectData.Buffer, MapInvalidChars(&total),
	    0, &total_perf);
	if (!NT_SUCCESS(status)) {
		TraceEvent(TRACE_ERROR,
		    "%s:%d: AddZFSinPerf failed-status 0x%x\n",
		    __func__, __LINE__, status);
	}

	kmem_free(unicodeName.Buffer, sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN);
}


void ZFSinPerfVdevCollect(PCW_MASK_INFORMATION CollectData) {
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR)
	    * ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;

	spa_t *spa_perf = NULL;
	zpool_perf_counters total_perf_vdev = { 0 };
	mutex_enter(&spa_namespace_lock);
	while ((spa_perf = spa_next(spa_perf)) != NULL) {
		spa_config_enter(spa_perf, SCL_ALL, FTAG, RW_READER);
		vdev_t *vd = spa_perf->spa_root_vdev;
		char vdev_zpool[ZFS_MAX_DATASET_NAME_LEN] = { 0 };
		zpool_perf_counters perf_vdev = { 0 };

		for (int c = 0; c < vd->vdev_children; c++) {
			char *vdev_name = vd->vdev_child[c]->vdev_path;
			if (!vdev_name || !vdev_name[0])
				continue;

			snprintf(vdev_zpool, ZFS_MAX_DATASET_NAME_LEN, "%s_%s",
			    vdev_name + 5, spa_perf->
			    spa_name);
			// Neglecting first five characters of vdev_name

			ANSI_STRING ansi_vdev;
			RtlInitAnsiString(&ansi_vdev, vdev_zpool);
			status = RtlAnsiStringToUnicodeString(&unicodeName,
			    &ansi_vdev, FALSE);

			vdev_t *cvd = vd->vdev_child[c];

			update_perf(&cvd->vdev_stat_ex, &cvd->vdev_stat, NULL,
			    NULL, &perf_vdev);
			update_total_perf(&perf_vdev, &total_perf_vdev);

			status = AddZFSinPerfVdev(CollectData.Buffer,
			    MapInvalidChars(&unicodeName), 0, &perf_vdev);

			if (!NT_SUCCESS(status)) {
				TraceEvent(
				    TRACE_ERROR,
			    "%s:%d: AddZFSinPerfVdev failed-status 0x%x\n",
				    __func__, __LINE__, status);
			}
		}
		spa_config_exit(spa_perf, SCL_ALL, FTAG);
	}
	mutex_exit(&spa_namespace_lock);

	UNICODE_STRING total;
	RtlInitUnicodeString(&total, L"_Total");
	status = AddZFSinPerfVdev(CollectData.Buffer, MapInvalidChars(&total),
	    0, &total_perf_vdev);
	if (!NT_SUCCESS(status)) {
		TraceEvent(TRACE_ERROR,
		    "%s:%d: AddZFSinPerfVdev failed-status 0x%x\n",
		    __func__, __LINE__, status);
	}
	kmem_free(unicodeName.Buffer, sizeof (WCHAR)
	    * ZFS_MAX_DATASET_NAME_LEN);
}

extern kstat_t *perf_arc_ksp, *perf_zil_ksp;
void ZFSinCachePerfCollect(PCW_MASK_INFORMATION CollectData) {
	UNICODE_STRING unicodeName;
	unicodeName.Buffer = kmem_alloc(sizeof (WCHAR) *
	    ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	unicodeName.MaximumLength = ZFS_MAX_DATASET_NAME_LEN;

	ANSI_STRING ansi_spa;
	RtlInitAnsiString(&ansi_spa, "Total");

	NTSTATUS status = RtlAnsiStringToUnicodeString(&unicodeName,
	    &ansi_spa, FALSE);
	if (!NT_SUCCESS(status)) {
		TraceEvent(TRACE_ERROR,
		    "%s:%d: Ansi to Unicode string conversion failed for %Z\n",
		    __func__, __LINE__, &ansi_spa);
	} else {
		cache_counters perf_cache = { 0 };

		KSTAT_ENTER(perf_arc_ksp);
		int error = KSTAT_UPDATE(perf_arc_ksp, KSTAT_READ);
		if (!error)
			arc_cache_counters_perfmon(&perf_cache,
			perf_arc_ksp->ks_data);
		KSTAT_EXIT(perf_arc_ksp);

		KSTAT_ENTER(perf_zil_ksp);
		error = KSTAT_UPDATE(perf_zil_ksp, KSTAT_READ);
		if (!error)
			zil_cache_counters_perfmon(&perf_cache,
			    perf_zil_ksp->ks_data);
		KSTAT_EXIT(perf_zil_ksp);

		status = AddZFSinCachePerf(CollectData.Buffer,
		    MapInvalidChars(&unicodeName), 0, &perf_cache);
		if (!NT_SUCCESS(status)) {
			TraceEvent(TRACE_ERROR,
			    "%s:%d:AddZFSinCachePerf failed-status 0x%x\n",
			    __func__, __LINE__, status);
		}
	}
	kmem_free(unicodeName.Buffer,
	sizeof (WCHAR) * ZFS_MAX_DATASET_NAME_LEN);
}


void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	vfs_unbusy(zfsvfs->z_vfs);
}

static uint_t zfsdev_private_tsd;

dev_t
zfsdev_get_dev(void)
{
	return ((dev_t)tsd_get(zfsdev_private_tsd));
}

/* We can't set ->private method, so this function does nothing */
void
zfsdev_private_set_state(void *priv, zfsdev_state_t *zs)
{
	UNREFERENCED_PARAMETER(priv);
	UNREFERENCED_PARAMETER(zs);
}

/* Loop all zs looking for matching dev_t */
zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	dev_t dev = (dev_t)priv;
	zfsdev_state_t *zs;
	mutex_enter(&zfsdev_state_lock);
	zs = zfsdev_get_state(dev, ZST_ALL);
	mutex_exit(&zfsdev_state_lock);
	return (zs);
}

static NTSTATUS
zfsdev_open(dev_t dev, PIRP Irp)
{
	int error;
	int flags = 0;
	int devtype = 0;
	struct proc *p = current_proc();
	PAGED_CODE();

	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_get_state(minor(dev), ZST_ALL)) {
		mutex_exit(&zfsdev_state_lock);
		return (0);
	}
	error = zfsdev_state_init((void *)dev);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static NTSTATUS
zfsdev_release(dev_t dev, PIRP Irp)
{
	/* zfsdev_state_destroy() doesn't check for NULL, so pre-lookup here */
	void *priv;

	priv = (void *)(uintptr_t)minor(dev);
	zfsdev_state_t *zs = zfsdev_private_get_state(priv);
	if (zs != NULL)
		zfsdev_state_destroy(priv);
	return (0);
}

static NTSTATUS
zfsdev_ioctl(PDEVICE_OBJECT DeviceObject, PIRP Irp, int flag)
{
	uint_t len, vecnum;
	zfs_iocparm_t zit;
	zfs_cmd_t *zc;
	int error, rc;
	user_addr_t uaddr;
	ulong_t cmd = 0;
	caddr_t arg = NULL;

	PIO_STACK_LOCATION  irpSp;
	irpSp = IoGetCurrentIrpStackLocation(Irp);

	len = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	cmd = irpSp->Parameters.DeviceIoControl.IoControlCode;
	arg = irpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	// vecnum = cmd - CTL_CODE(ZFSIOCTL_TYPE, ZFSIOCTL_BASE,
	//  METHOD_NEITHER, FILE_ANY_ACCESS);

	vecnum = DEVICE_FUNCTION_FROM_CTL_CODE(cmd);
	ASSERT3U(vecnum, >=, ZFSIOCTL_BASE + ZFS_IOC_FIRST);
	ASSERT3U(vecnum, <, ZFSIOCTL_BASE + ZFS_IOC_LAST);
	vecnum -= ZFSIOCTL_BASE;

	if (len != sizeof (zfs_iocparm_t)) {
		/*
		 * printf("len %d vecnum: %d sizeof (zfs_cmd_t) %lu\n",
		 *  len, vecnum, sizeof (zfs_cmd_t));
		 */
		return (EINVAL);
	}

	// Copy in the wrapper, which contains real zfs_cmd_t addr, len,
	// and compat version
	error = ddi_copyin((void *)arg, &zit, len, 0);
	if (error != 0)
		return (EINVAL);

	uaddr = (user_addr_t)zit.zfs_cmd;

	// get ready for zfs_cmd_t
	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	if (copyin((void *)uaddr, zc, sizeof (zfs_cmd_t))) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	error = zfsdev_ioctl_common(vecnum, zc, 0);

	rc = copyout(zc, (void *)uaddr, sizeof (zfs_cmd_t));

	if (error == 0 && rc != 0)
		error = -SET_ERROR(EFAULT);

	// Set the real return code in struct.
	// XNU only calls copyout if error=0, but
	// presumably we can skip that in Windows and just return?
	zit.zfs_ioc_error = error;
	error = ddi_copyout(&zit, (void *)arg, len, 0);
	error = 0;

out:
	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);

}

/*
 * inputs:
 * zc_name		dataset name to mount
 * zc_value	path location to mount
 *
 * outputs:
 * return code
 */
int zfs_windows_mount(zfs_cmd_t *zc);  // move me to headers

static int
zfs_ioc_mount(zfs_cmd_t *zc)
{
	return (zfs_windows_mount(zc));
}

/*
 * inputs:
 * zc_name		dataset name to unmount
 * zc_value	path location to unmount
 *
 * outputs:
 * return code
 */
int zfs_windows_unmount(zfs_cmd_t *zc); // move me to headers

static int
zfs_ioc_unmount(zfs_cmd_t *zc)
{
	dprintf("%s: enter\n", __func__);
	return (zfs_windows_unmount(zc));
}

void
zfs_ioctl_init_os(void)
{
	/*
	 * Windows functions
	 */
	zfs_ioctl_register_legacy(ZFS_IOC_MOUNT, zfs_ioc_mount,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_UNMOUNT, zfs_ioc_unmount,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);

}


/* ioctl handler for block device. Relay to zvol */
static int
zfsdev_bioctl(dev_t dev, ulong_t cmd, caddr_t data,
    __unused int flag, struct proc *p)
{
	return (zvol_os_ioctl(dev, cmd, data, 1, NULL, NULL));
}

// Callback to print registered filesystems. Not needed
void
DriverNotificationRoutine(_In_ struct _DEVICE_OBJECT *DeviceObject,
    _In_ BOOLEAN FsActive)
{
	CHAR nibuf[512]; // buffer that receives name information and name
	POBJECT_NAME_INFORMATION name_info = (POBJECT_NAME_INFORMATION)nibuf;
	ULONG ret_len;
	NTSTATUS status;

	status = ObQueryNameString(DeviceObject, name_info,
	    sizeof (nibuf), &ret_len);
	if (NT_SUCCESS(status)) {
		dprintf("Filesystem %p: '%wZ'\n", DeviceObject,
		    &name_info->Name);
	} else {
		dprintf("Filesystem %p: '%wZ'\n", DeviceObject,
		    &DeviceObject->DriverObject->DriverName);
	}
}

// extern PDRIVER_UNLOAD STOR_DriverUnload;
uint64_t
zfs_ioc_unregister_fs(void)
{
	dprintf("%s\n", __func__);
	if (zfs_module_busy != 0) {
		dprintf("%s: datasets still busy: %llu pool(s)\n", __func__,
		    zfs_module_busy);
		return (zfs_module_busy);
	}
	if (fsDiskDeviceObject != NULL) {
		IoUnregisterFsRegistrationChange(WIN_DriverObject,
		    DriverNotificationRoutine);
		IoUnregisterFileSystem(fsDiskDeviceObject);
		ObDereferenceObject(fsDiskDeviceObject);
		UNICODE_STRING ntWin32NameString;
		RtlInitUnicodeString(&ntWin32NameString, ZFS_DEV_DOS);
		IoDeleteSymbolicLink(&ntWin32NameString);
		IoDeleteDevice(fsDiskDeviceObject);
		fsDiskDeviceObject = NULL;
	}
#if 0
	// Do not unload these, so that the zfsinstaller uninstall can
	// find the devnode to trigger uninstall.
	if (STOR_DriverUnload != NULL) {
		STOR_DriverUnload(WIN_DriverObject);
		STOR_DriverUnload = NULL;
	}
#endif
	return (0);
}

#ifdef ZFS_DEBUG
#define	ZFS_DEBUG_STR	" (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR	""
#endif

static int
openzfs_init_os(void)
{
	return (0);
}

static void
openzfs_fini_os(void)
{
}

int
zfsdev_attach(void)
{
	NTSTATUS ntStatus;
	UNICODE_STRING  ntUnicodeString;    // NT Device Name
	UNICODE_STRING ntWin32NameString; // Win32 Name
	int err;

	static UNICODE_STRING sddl = RTL_CONSTANT_STRING(
	    L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)"
	    "(A;;GRGWGX;;;WD)(A;;GRGX;;;RC)");
	// Or use &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R

	RtlInitUnicodeString(&ntUnicodeString, ZFS_DEV_KERNEL);
	ntStatus = IoCreateDeviceSecure(
	    WIN_DriverObject,
	    sizeof (mount_t),
	    &ntUnicodeString, // Device name "\Device\SIOCTL"
	    FILE_DEVICE_UNKNOWN, // Device type
	    /* FILE_DEVICE_SECURE_OPEN */ 0, // Device characteristics
	    FALSE, // Not an exclusive device
	    &sddl,
	    NULL,
	    &ioctlDeviceObject); // Returned ptr to Device Object

	if (!NT_SUCCESS(ntStatus)) {
		dprintf("ZFS: Couldn't create the device object "
		    "/dev/zfs (%S)\n", ZFS_DEV_KERNEL);
		return (ntStatus);
	}
	dprintf("ZFS: created kernel device node: %p: name %S\n",
	    ioctlDeviceObject, ZFS_DEV_KERNEL);

	UNICODE_STRING fsDiskDeviceName;
	RtlInitUnicodeString(&fsDiskDeviceName, ZFS_GLOBAL_FS_DISK_DEVICE_NAME);

	ntStatus = IoCreateDeviceSecure(WIN_DriverObject, // DriverObject
	    sizeof (mount_t),  // DeviceExtensionSize
	    &fsDiskDeviceName, // DeviceName
	    FILE_DEVICE_DISK_FILE_SYSTEM, // DeviceType
	    0,
	    FALSE,
	    &sddl,
	    NULL,
	    &fsDiskDeviceObject); // DeviceObject

	ObReferenceObject(ioctlDeviceObject);

	mount_t *dgl;
	dgl = ioctlDeviceObject->DeviceExtension;
	dgl->type = MOUNT_TYPE_DGL;
	dgl->size = sizeof (mount_t);

	mount_t *vcb;
	vcb = fsDiskDeviceObject->DeviceExtension;
	vcb->type = MOUNT_TYPE_VCB;
	vcb->size = sizeof (mount_t);

	if (ntStatus == STATUS_SUCCESS) {
		dprintf("DiskFileSystemDevice: 0x%0x  %wZ created\n",
		    ntStatus, &fsDiskDeviceName);
	}

	// Initialize a Unicode String containing the Win32 name
	// for our device.
	RtlInitUnicodeString(&ntWin32NameString, ZFS_DEV_DOS);

	// Create a symbolic link between our device name  and the Win32 name
	ntStatus = IoCreateSymbolicLink(
	    &ntWin32NameString, &ntUnicodeString);

	if (!NT_SUCCESS(ntStatus)) {
		dprintf("ZFS: Couldn't create userland symbolic link to "
		    "/dev/zfs (%wZ)\n", ZFS_DEV);
		ObDereferenceObject(ioctlDeviceObject);
		IoDeleteDevice(ioctlDeviceObject);
		return (-1);
	}
	dprintf("ZFS: created userland device symlink\n");

	fsDiskDeviceObject->Flags |= DO_DIRECT_IO;
	fsDiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	IoRegisterFileSystem(fsDiskDeviceObject);
	ObReferenceObject(fsDiskDeviceObject);

	NTSTATUS pcwStatus = RegisterZFSinPerf(ZFSinPerfCallBack, NULL);
	if (!NT_SUCCESS(pcwStatus)) {
		TraceEvent(TRACE_ERROR, "ZFSin perf registration failed\n");
	}
	pcwStatus = RegisterZFSinPerfVdev(ZFSinPerfVdevCallBack, NULL);
	if (!NT_SUCCESS(pcwStatus)) {
		TraceEvent(TRACE_ERROR,
		    "ZFSin vdev perf registration failed\n");
	}
	pcwStatus = RegisterZFSinCachePerf(ZFSinCachePerfCallBack, NULL);
	if (!NT_SUCCESS(pcwStatus)) {
		TraceEvent(TRACE_ERROR,
		    "ZFSin cache perf registration failed\n");
	}


	// Set all the callbacks to "dispatch()"
	WIN_DriverObject->MajorFunction[IRP_MJ_CREATE] =
	    (PDRIVER_DISPATCH)dispatcher;   // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_CLOSE] =
	    (PDRIVER_DISPATCH)dispatcher;	// zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_READ] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_WRITE] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_EA] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_EA] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher; // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher; // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_CLEANUP] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DEVICE_CHANGE] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_PNP] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY] =
	    (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_SECURITY] =
	    (PDRIVER_DISPATCH)dispatcher;

	// Dump all registered filesystems
	ntStatus = IoRegisterFsRegistrationChange(WIN_DriverObject,
	    DriverNotificationRoutine);

	if ((err = zcommon_init()) != 0)
		goto zcommon_failed;
	if ((err = icp_init()) != 0)
		goto icp_failed;
	if ((err = zstd_init()) != 0)
		goto zstd_failed;
	if ((err = openzfs_init_os()) != 0)
		goto openzfs_os_failed;

	tsd_create(&zfsdev_private_tsd, NULL);

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "ZFS: Loaded module %s, "
	    "ZFS pool version %s, ZFS filesystem version %s\n",
	    ZFS_META_GITREV,
	    SPA_VERSION_STRING, ZPL_VERSION_STRING);

	return (0);

openzfs_os_failed:
	zstd_fini();
zstd_failed:
	icp_fini();
icp_failed:
	zcommon_fini();
zcommon_failed:
	return (err);
}

void
zfsdev_detach(void)
{
	zfsdev_state_t *zs, *zsprev = NULL;

	UnregisterZFSinPerf();
	UnregisterZFSinPerfVdev();
	UnregisterZFSinCachePerf();

	PDEVICE_OBJECT deviceObject = WIN_DriverObject->DeviceObject;
	UNICODE_STRING uniWin32NameString;

	RtlInitUnicodeString(&uniWin32NameString, ZFS_DEV_DOS);
	IoDeleteSymbolicLink(&uniWin32NameString);
	if (deviceObject != NULL) {
		ObDereferenceObject(deviceObject);
		IoDeleteDevice(deviceObject);
	}

	tsd_destroy(&zfsdev_private_tsd);

	openzfs_fini_os();
	zstd_fini();
	icp_fini();
	zcommon_fini();

}

/* Update the VFS's cache of mountpoint properties */
void
zfs_ioctl_update_mount_cache(const char *dsname)
{
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(dsname, &zfsvfs) == 0) {
		/* insert code here */
		zfs_vfs_rele(zfsvfs);
	}
	/*
	 * Ignore errors; we can't do anything useful if either getzfsvfs or
	 * VFS_STATFS fails.
	 */
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (KMALLOC_MAX_SIZE);
}
