// SPDX-License-Identifier: BSD-2-Clause
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/vm.h>

const int zfs_vm_pagerret_bad = -1;
const int zfs_vm_pagerret_error = -2;
const int zfs_vm_pagerret_ok = 0;
const int zfs_vm_pagerret_pend = 1;
const int zfs_vm_pagerput_sync = 2;
const int zfs_vm_pagerput_inval = 3;

void
zfs_vmobject_assert_wlocked(vm_object_t object)
{
	(void) object;
}

void
zfs_vmobject_wlock(vm_object_t object)
{
	if (object != NULL && object->vmobjlock != NULL)
		rw_enter(object->vmobjlock, RW_WRITER);
}

void
zfs_vmobject_wunlock(vm_object_t object)
{
	if (object != NULL && object->vmobjlock != NULL)
		rw_exit(object->vmobjlock);
}
