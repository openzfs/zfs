#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# A directory based xattr object with a damaged SA must not panic the
# kernel.  Reading it must fail, and the file owning it must still be
# removable, also across a remount (unlinked drain).
#
# STRATEGY:
#	1. Create a pool with a xattr=dir filesystem, a file and an xattr.
#	2. Export the pool and damage the SA magic of the xattr object.
#	3. Import the pool and verify reading the xattr fails without panic.
#	4. Remove the file and verify the xattr object is gone.
#	5. Remount and verify the filesystem is usable.
#	6. Crash scenario: with unlinked processing suspended, remove a second
#	   file with a damaged xattr object, export, resume processing and
#	   import.  The mount-time unlinked drain must not panic and must
#	   free the damaged object (checked via zdb and the debug log).
#

verify_runnable "global"

TESTPOOL2=xattr_corrupt_pool
VDIR=$TEST_BASE_DIR/xattr_015
FS=$TESTPOOL2/fs

function cleanup
{
	is_linux && set_tunable32 UNLINK_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -rf $VDIR
}
log_onexit cleanup

log_assert "A damaged xattr SA gives an error, not a panic, and is removable."

log_must mkdir -p $VDIR
log_must truncate -s 128m $VDIR/vdev
log_must zpool create -f -O mountpoint=$VDIR/mnt $TESTPOOL2 $VDIR/vdev
log_must zfs create -o xattr=dir -o mountpoint=$VDIR/fs $FS

log_must touch $VDIR/fs/file
log_must set_xattr testattr "value" $VDIR/fs/file
log_must get_xattr testattr $VDIR/fs/file

typeset fobj=$(get_objnum $VDIR/fs/file)
sync_pool $TESTPOOL2
typeset xdir=$(zdb -dddd $FS $fobj | awk '$1 == "xattr" {print $2}')
[[ -n $xdir ]] || log_fail "no xattr directory for object $fobj"
typeset xobj=$(zdb -dddd $FS $xdir | awk '/testattr =/ {print $3}')
[[ -n $xobj ]] || log_fail "no xattr object in directory $xdir"
log_note "file=$fobj xattr dir=$xdir xattr object=$xobj"

log_must zpool export $TESTPOOL2
log_must corrupt_sa $VDIR $TESTPOOL2 $FS $xobj

log_must zpool import -d $VDIR $TESTPOOL2

# Reading the xattr must fail cleanly.
log_mustnot get_xattr testattr $VDIR/fs/file
log_must test -f $VDIR/fs/file

# The file and its damaged xattr must be removable.
log_must rm -f $VDIR/fs/file
log_must sync_pool $TESTPOOL2

# Remount: the unlinked drain must not trip over the damaged object.
log_must zfs unmount $FS
log_must zfs mount $FS
log_must touch $VDIR/fs/file2
log_must set_xattr testattr2 "value" $VDIR/fs/file2
log_must get_xattr testattr2 $VDIR/fs/file2

log_must zpool export $TESTPOOL2
log_must zpool import -d $VDIR $TESTPOOL2
log_must zpool scrub -w $TESTPOOL2

# The crash scenario needs to suspend unlinked processing, which is only
# possible on Linux (zfs_unlink_suspend_progress).
if is_linux; then
	# Crash scenario: the file is gone, but its xattr directory is still in the
	# unlinked set, so the next mount has to drain it.
	log_must touch $VDIR/fs/file3
	log_must set_xattr testattr3 "value" $VDIR/fs/file3
	fobj=$(get_objnum $VDIR/fs/file3)
	sync_pool $TESTPOOL2
	xdir=$(zdb -dddd $FS $fobj | awk '$1 == "xattr" {print $2}')
	[[ -n $xdir ]] || log_fail "no xattr directory for object $fobj"
	xobj=$(zdb -dddd $FS $xdir | awk '/testattr3 =/ {print $3}')
	[[ -n $xobj ]] || log_fail "no xattr object in directory $xdir"
	log_note "file=$fobj xattr dir=$xdir xattr object=$xobj"

	log_must zpool export $TESTPOOL2
	log_must corrupt_sa $VDIR $TESTPOOL2 $FS $xobj
	log_must zpool import -d $VDIR $TESTPOOL2

	log_must set_tunable32 UNLINK_SUSPEND_PROGRESS 1
	log_must rm -f $VDIR/fs/file3
	log_must sync_pool $TESTPOOL2
	log_must zpool export $TESTPOOL2
	log_must set_tunable32 UNLINK_SUSPEND_PROGRESS 0

	# The drain at import/mount time must not panic.
	log_must zpool import -d $VDIR $TESTPOOL2
	log_must sync_pool $TESTPOOL2
	log_must zpool export $TESTPOOL2

	# Both the xattr directory and the damaged object must be freed.
	log_mustnot eval "zdb -e -p $VDIR -dddd $FS $xobj | grep -q 'ZFS plain file'"
	log_mustnot eval "zdb -e -p $VDIR -dddd $FS $xdir | grep -q 'ZFS directory'"
	log_must zpool import -d $VDIR $TESTPOOL2
	log_must zpool scrub -w $TESTPOOL2
fi

log_pass "A damaged xattr SA gives an error, not a panic, and is removable."
