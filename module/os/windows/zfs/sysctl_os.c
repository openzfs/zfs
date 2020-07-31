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
 * Copyright(c) 2022 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/zfs_context.h>
#include <sys/linker_set.h>
#include <sys/mod_os.h>
#include <sys/types.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>

#include <sys/arc_impl.h>
#include <sys/dsl_pool.h>

/* Wait for Registry changes */
static WORK_QUEUE_ITEM wqi;
static HANDLE registry_notify_fd = 0;
static UNICODE_STRING sysctl_os_RegistryPath;

void sysctl_os_init(PUNICODE_STRING RegistryPath);

extern uint32_t spl_hostid;

HANDLE
sysctl_os_open_registry(PUNICODE_STRING pRegistryPath)
{
	OBJECT_ATTRIBUTES ObjectAttributes;
	HANDLE h;
	NTSTATUS Status;

	InitializeObjectAttributes(&ObjectAttributes,
	    pRegistryPath,
	    OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
	    NULL,
	    NULL);

	Status = ZwCreateKey(&h,	// KeyHandle
	    KEY_ALL_ACCESS | KEY_CREATE_SUB_KEY | KEY_NOTIFY,	// DesiredAccess
	    &ObjectAttributes,
	    0,
	    NULL,
	    REG_OPTION_NON_VOLATILE,
	    NULL);

	if (!NT_SUCCESS(Status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "%s: Unable to open Registry %wZ: 0x%x -- skipping tunables\n",
		    __func__, pRegistryPath, Status));
		return (0);
	}
	return (h);
}

void
sysctl_os_close_registry(HANDLE regfd)
{
	ZwClose(regfd);
}

int
sysctl_os_write_registry(HANDLE regfd, ztunable_t *zt, UNICODE_STRING *entry)
{
	void *val = NULL;
	ULONG len = 0;
	ULONG type = 0; // Registry type
	UNICODE_STRING str = { 0 };
	NTSTATUS Status;
	ULONG length;

	ZT_GET_VALUE(zt, &val, &len, &type);

	ASSERT3P(val, !=, NULL);

	if (type == ZT_TYPE_STRING) {

		/*
		 * STRINGS: from zfs/ZT struct to write out to Registry
		 * Check how much space convert will need, allocate
		 * buffer
		 * Convert ascii -> utf8 the string
		 * Assign to Registry update.
		 */
		Status = RtlUTF8ToUnicodeN(NULL, 0,
		    &length, val, len);
		if (!NT_SUCCESS(Status))
			goto skip;
		if (length == 0) length++;
		str.Length = str.MaximumLength = length;
		str.Buffer = ExAllocatePoolWithTag(PagedPool, length,
		    'ZTST');
		if (str.Buffer == NULL) {
			Status = STATUS_NO_MEMORY;
			goto skip;
		}

		Status = RtlUTF8ToUnicodeN(str.Buffer,
		    str.MaximumLength, &length, val, len);
		str.Length = length;

		len = length;
		val = str.Buffer;

		if (!NT_SUCCESS(Status))
			goto skip;
	}

	Status = ZwSetValueKey(
	    regfd,
	    entry,
	    0,
	    ZT_TYPE_REGISTRY(type),
	    val,
	    len);

skip:
	if ((type == ZT_TYPE_STRING) &&
	    str.Buffer != NULL)
		ExFreePool(str.Buffer);

	return (Status);
}

void
sysctl_os_process(PUNICODE_STRING pRegistryPath, ztunable_t *zt)
{
	HANDLE regfd;

	dprintf(
	    "tunable: '%s/%s' type %d at %p\n",
	    zt->zt_prefix, zt->zt_name,
	    zt->zt_type,
	    zt->zt_ptr);

	/*
	 * tunable: 'zfs_prefetch_disable' type 0 at FFFFF80731A1B770
	 * tunable: 'zfs_prefetch_max_streams' type 1 at FFFFF80730769404
	 * tunable: 'zfs_prefetch_array_rd_sz' type 3 at FFFFF80730768EC8
	 */

	/*
	 * For each tunable;
	 * - check if registry entry exists
	 * - no; create entry and set value of tunable.
	 * - yes; read registry value and set tunable (if different)
	 */
	NTSTATUS Status;
	ULONG length;

	// Linux MODULEPARAM limit is 1024
	static DECLARE_UNICODE_STRING_SIZE(entry, LINUX_MAX_MODULE_PARAM_LEN);

	// Use RegistryPath
	Status = RtlUnicodeStringCopy(&entry, pRegistryPath);
	if (!NT_SUCCESS(Status))
		return;

	// Add backslash?
	Status = RtlUnicodeStringCatString(&entry, L"\\");
	if (!NT_SUCCESS(Status))
		return;

	// keys are "prefix", add to entry
	Status = RtlUTF8ToUnicodeN(
	    (PWSTR)&((uchar_t *)entry.Buffer)[entry.Length],
	    LINUX_MAX_MODULE_PARAM_LEN - entry.Length,
	    &length, zt->zt_prefix, strlen(zt->zt_prefix));
	entry.Length += length;

	// If we failed to convert it, just skip it.
	if (Status != STATUS_SUCCESS &&
	    Status != STATUS_SOME_NOT_MAPPED)
		return;

	// Open registry
	regfd = sysctl_os_open_registry(&entry);
	if (regfd == 0)
		return;

	// create key entry name
	Status = RtlUTF8ToUnicodeN(entry.Buffer, LINUX_MAX_MODULE_PARAM_LEN,
	    &length, zt->zt_name, strlen(zt->zt_name));
	entry.Length = length;

	// If we failed to convert it, just skip it.
	if (Status != STATUS_SUCCESS &&
	    Status != STATUS_SOME_NOT_MAPPED)
		return;

	// Do we have key?
	Status = ZwQueryValueKey(
	    regfd,
	    &entry,
	    KeyValueFullInformation,
	    NULL,
	    0,
	    &length);

	/* Some tunables need to always be written, think zfs_version */
	if (zt->zt_flag & ZT_FLAG_WRITEONLY)
		Status = STATUS_OBJECT_NAME_NOT_FOUND;

	if (Status == STATUS_OBJECT_NAME_NOT_FOUND) {

		Status = sysctl_os_write_registry(regfd, zt, &entry);

	} else {
		// Has entry in Registry, read it, and update tunable
		// Biggest value we store at the moment is uint64_t
		uchar_t *buffer;
		buffer = ExAllocatePoolWithTag(PagedPool, length, '!SFZ');
		if (buffer == NULL)
			goto failed;

		Status = ZwQueryValueKey(
		    regfd,
		    &entry,
		    KeyValueFullInformation,
		    buffer,
		    length,
		    &length);

		if (NT_SUCCESS(Status)) {
			char *strval = NULL;
			KEY_VALUE_FULL_INFORMATION *kv =
			    (KEY_VALUE_FULL_INFORMATION *)buffer;
			void *val = NULL;
			ULONG len = 0;
			ULONG type = 0;

			// _CALL style has to 'type', so look it up first.
			if (zt->zt_perm == ZT_ZMOD_RW) {
				char **maybestr = NULL;
				ZT_GET_VALUE(zt, &maybestr, &len, &type);

				// Set up buffers to SET value.
				val = &buffer[kv->DataOffset];
				len = kv->DataLength;

				// If string, make ascii
				if (type == ZT_TYPE_STRING) {
					/*
					 * STRINGS:
					 *
					 * Static? Convert into buffer assuming
					 * static MAX.
					 * Dynamic?
					 * First if it has a value and
					 * ALLOCATED, then free().
					 * Check string kength needed, allocate
					 * Then convert ascii -> utf8 the string
					 */
					/* Already set? free it */
					if (!(zt->zt_flag & ZT_FLAG_STATIC)) {

						if (maybestr != NULL &&
						    *maybestr != NULL)
							ExFreePool(*maybestr);

						*maybestr = NULL;
					}
					/* How much space needed? */
					Status = RtlUnicodeToUTF8N(NULL, 0,
					    &length, val, len);
					if (!NT_SUCCESS(Status))
						goto failed;

					/* Get space */
					strval = ExAllocatePoolWithTag(
					    PagedPool, length + 1, 'ZTST');
					if (strval == NULL)
						goto failed;

					/* Convert to ascii */
					Status = RtlUnicodeToUTF8N(strval,
					    length, &length, val, len);
					if (!NT_SUCCESS(Status))
						goto failed;

					strval[length] = 0;
					val = strval;

				}

				ZT_SET_VALUE(zt, &val, &len, &type);

				if ((zt->zt_flag & ZT_FLAG_STATIC) &&
				    strval != NULL) {
					ExFreePoolWithTag(strval, '!SFZ');
				}


				/*
				 * If the registry exists, it is written to by
				 * user, the actual value may be changed by the
				 * _set functions, so we need to call GET again,
				 * and if it differs, update Registry with real
				 * (new) value.
				 * So if its a call-out type, it could have been
				 * adjusted by the call.
				 */
				if (zt->zt_func != NULL) {
					Status = sysctl_os_write_registry(regfd,
					    zt, &entry);
				}


			} // RD vs RW
		}

failed:
		if (buffer != NULL)
			ExFreePoolWithTag(buffer, '!SFZ');
	}

	// Close registry
	sysctl_os_close_registry(regfd);

}

static void
sysctl_os_fix(void)
{
	uint64_t allmem = arc_all_memory();

#ifdef __LP64__
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(4ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#else
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(1ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#endif

	if (zfs_dirty_data_max == 0) {
		zfs_dirty_data_max = allmem *
		    zfs_dirty_data_max_percent / 100;
		zfs_dirty_data_max = MIN(zfs_dirty_data_max,
		    zfs_dirty_data_max_max);
	}

	if (zfs_wrlog_data_max == 0) {
		zfs_wrlog_data_max = zfs_dirty_data_max * 2;
	}

}

_Function_class_(WORKER_THREAD_ROUTINE) void __stdcall
sysctl_os_registry_change(PVOID Parameter)
{
	PUNICODE_STRING RegistryPath = Parameter;

	IO_STATUS_BLOCK iosb;

	// open if first time here
	if (registry_notify_fd == 0) {

		registry_notify_fd = sysctl_os_open_registry(RegistryPath);

		if (registry_notify_fd != 0) {
			RtlDuplicateUnicodeString(
			    RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING |
			    RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
			    RegistryPath,
			    &sysctl_os_RegistryPath);
			ExInitializeWorkItem(&wqi, sysctl_os_registry_change,
			    &sysctl_os_RegistryPath);
		}
	} else {
		// Notified, re-scan registry
		sysctl_os_init(RegistryPath);
		// Some tunables must not be unset.
		sysctl_os_fix();
	}

	if (registry_notify_fd == 0)
		return;

	// Arm / Re-Arm
	ZwNotifyChangeKey(registry_notify_fd, NULL,
	    (PVOID)&wqi, (PVOID)DelayedWorkQueue,
	    &iosb,
	    REG_NOTIFY_CHANGE_LAST_SET,
	    TRUE, NULL, 0, TRUE);
}

/*
 * ZFS_MODULE_PARAM() will create a ztunable_t struct for
 * each tunable, so at startup, iterate the "zt" linker-set
 * to access all tunables.
 */
void
sysctl_os_init(PUNICODE_STRING RegistryPath)
{
	// iterate the linker-set
	ztunable_t **iter = NULL;
	int count = 0;

	SET_DECLARE(zt, ztunable_t);

	SET_FOREACH(iter, zt) {
		if (iter != NULL && *iter != NULL) { // Sanity
			ztunable_t *zt = *iter;

			sysctl_os_process(RegistryPath, zt);
			count++;
		}
	}
}

void
sysctl_os_fini(void)
{
	HANDLE fd = registry_notify_fd;
	registry_notify_fd = 0;
	RtlFreeUnicodeString(&sysctl_os_RegistryPath);

	if (fd != 0)
		ZwClose(fd);
}

int
param_set_arc_max(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	if (set == B_FALSE) {
		*ptr = &zfs_arc_max;
		*len = sizeof (zfs_arc_max);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (zfs_arc_max));

	val = *(uint64_t *)(*ptr);

	if (val != 0 && (val < MIN_ARC_MAX || val <= arc_c_min ||
	    val >= arc_all_memory()))
		return (SET_ERROR(EINVAL));

	zfs_arc_max = val;
	arc_tuning_update(B_TRUE);

	/* Update the sysctl to the tuned value */
	if (val != 0)
		zfs_arc_max = arc_c_max;

	return (0);
}

int
param_set_arc_min(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	if (set == B_FALSE) {
		*ptr = &zfs_arc_min;
		*len = sizeof (zfs_arc_min);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (zfs_arc_min));

	val = *(uint64_t *)(*ptr);

	if (val != 0 && (val < 2ULL << SPA_MAXBLOCKSHIFT || val > arc_c_max))
		return (SET_ERROR(EINVAL));

	zfs_arc_min = val;
	arc_tuning_update(B_TRUE);

	if (val != 0)
		zfs_arc_min = arc_c_min;

	return (0);
}

static int
sysctl_vfs_zfs_arc_no_grow_shift(ZFS_MODULE_PARAM_ARGS)
{
	int val;

	*type = ZT_TYPE_INT;

	if (set == B_FALSE) {
		*ptr = &arc_no_grow_shift;
		*len = sizeof (arc_no_grow_shift);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (arc_no_grow_shift));

	val = *(int *)(*ptr);

	if (val < 0 || val >= arc_shrink_shift)
		return (EINVAL);

	arc_no_grow_shift = val;
	return (0);
}

int
param_set_arc_u64(ZFS_MODULE_PARAM_ARGS)
{
	*ptr = zt->zt_ptr;
	*len = sizeof (uint64_t);
	*type = ZT_TYPE_U64;

	arc_tuning_update(B_TRUE);

	return (0);
}

int
param_set_arc_int(ZFS_MODULE_PARAM_ARGS)
{
	*ptr = zt->zt_ptr;
	*len = sizeof (uint64_t);
	*type = ZT_TYPE_U64;

	arc_tuning_update(B_TRUE);

	return (0);
}

/* dbuf.c */

/* dmu.c */

/* dmu_zfetch.c */

/* dsl_pool.c */

/* dnode.c */

/* dsl_scan.c */

/* metaslab.c */

/* spa_misc.c */
extern int zfs_flags;
static int
sysctl_vfs_zfs_debug_flags(ZFS_MODULE_PARAM_ARGS)
{
	int val;

	*type = ZT_TYPE_INT;

	if (set == B_FALSE) {
		*ptr = &zfs_flags;
		*len = sizeof (zfs_flags);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (zfs_flags));

	val = *(int *)(*ptr);

	/*
	 * ZFS_DEBUG_MODIFY must be enabled prior to boot so all
	 * arc buffers in the system have the necessary additional
	 * checksum data.  However, it is safe to disable at any
	 * time.
	 */
	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		val &= ~ZFS_DEBUG_MODIFY;
	zfs_flags = val;

	return (0);
}

int
param_set_deadman_synctime(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	if (set == B_FALSE) {
		*ptr = &zfs_deadman_synctime_ms;
		*len = sizeof (zfs_deadman_synctime_ms);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (zfs_deadman_synctime_ms));

	val = *(uint64_t *)(*ptr);

	zfs_deadman_synctime_ms = val;

	spa_set_deadman_synctime(MSEC2NSEC(zfs_deadman_synctime_ms));

	return (0);
}

int
param_set_deadman_ziotime(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	if (set == B_FALSE) {
		*ptr = &zfs_deadman_ziotime_ms;
		*len = sizeof (zfs_deadman_ziotime_ms);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (zfs_deadman_ziotime_ms));

	val = *(uint64_t *)(*ptr);

	zfs_deadman_ziotime_ms = val;

	spa_set_deadman_ziotime(MSEC2NSEC(zfs_deadman_synctime_ms));

	return (0);
}

int
param_set_deadman_failmode(ZFS_MODULE_PARAM_ARGS)
{
	char buf[16];

	*type = ZT_TYPE_STRING;

	if (set == B_FALSE) {
		*ptr = (void *)zfs_deadman_failmode;
		*len = strlen(zfs_deadman_failmode) + 1;
		return (0);
	}

	strlcpy(buf, *ptr, sizeof (buf));

	if (strcmp(buf, zfs_deadman_failmode) == 0)
		return (0);
	if (strcmp(buf, "wait") == 0)
		zfs_deadman_failmode = "wait";
	if (strcmp(buf, "continue") == 0)
		zfs_deadman_failmode = "continue";
	if (strcmp(buf, "panic") == 0)
		zfs_deadman_failmode = "panic";

	return (-param_set_deadman_failmode_common(buf));
}


/* spacemap.c */

/* vdev.c */
int
param_set_min_auto_ashift(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	*ptr = &zfs_vdev_min_auto_ashift;
	*len = sizeof (zfs_vdev_min_auto_ashift);

	val = zfs_vdev_min_auto_ashift;

	if (val < ASHIFT_MIN || val > zfs_vdev_max_auto_ashift)
		return (SET_ERROR(EINVAL));

	zfs_vdev_min_auto_ashift = val;

	return (0);
}

int
param_set_max_auto_ashift(ZFS_MODULE_PARAM_ARGS)
{
	uint64_t val;

	*type = ZT_TYPE_U64;

	*ptr = &zfs_vdev_max_auto_ashift;
	*len = sizeof (zfs_vdev_max_auto_ashift);

	val = zfs_vdev_max_auto_ashift;

	if (val > ASHIFT_MAX || val < zfs_vdev_min_auto_ashift)
		return (SET_ERROR(EINVAL));

	zfs_vdev_max_auto_ashift = val;

	return (0);
}

int
param_set_slop_shift(ZFS_MODULE_PARAM_ARGS)
{
	int val;

	*type = ZT_TYPE_INT;

	if (set == B_FALSE) {
		*ptr = &spa_slop_shift;
		*len = sizeof (spa_slop_shift);
		return (0);
	}

	ASSERT3U(*len, >=, sizeof (spa_slop_shift));

	val = *(int *)(*ptr);

	if (val < 1 || val > 31)
		return (EINVAL);

	spa_slop_shift = val;

	return (0);
}

int
param_set_multihost_interval(ZFS_MODULE_PARAM_ARGS)
{
	*ptr = zt->zt_ptr;
	*len = sizeof (uint64_t);
	*type = ZT_TYPE_U64;

	if (spa_mode_global != SPA_MODE_UNINIT)
		mmp_signal_all_threads();

	return (0);
}
