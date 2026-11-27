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

#
# DESCRIPTION:
#	Rolling back a mounted filesystem must leave a child filesystem
#	mounted below it, the child must still unmount and be destroyed
#	afterwards, and a new child of the same name must mount (#12460).
#
# STRATEGY:
#	1. Create a parent filesystem and snapshot it.
#	2. Create a child after the snapshot, so its mountpoint directory
#	   is not in the snapshot, and write a file in it.
#	3. Roll the parent back to the snapshot while the child is mounted.
#	4. Verify the child is still mounted, its file reads back (Linux),
#	   and 'zfs destroy' (which unmounts it) succeeds.  Linux before
#	   86db35c447 detached the child's mount in the rollback, and the
#	   destroy failed with "unmount failed".
#	5. Repeat: the new child must mount and be writable.  After step 4
#	   the parent's dentry for the old mountpoint directory kept its
#	   stale inode, so the mountpoint lookup returned EIO and the new
#	   child failed to mount ("failed to create mountpoint").
#	6. Repeat with a child that existed when the snapshot was taken.
#

verify_runnable "both"

typeset parent=$TESTPOOL/$TESTFS/rb_child_mount
typeset child=$parent/child
typeset -i i

function cleanup
{
	datasetexists $parent && destroy_dataset $parent -Rf
}

function check_child # iteration
{
	typeset mntpt=$(get_prop mountpoint $child)

	log_must ismounted $child
	[[ "$(get_prop mounted $child)" == "yes" ]] || \
	    log_fail "$1: $child is not mounted after the rollback"
	#
	# On Linux the child stays reachable at its path.  On FreeBSD the
	# rollback removed the mountpoint directory from the parent, so the
	# path no longer leads to the (still mounted) child.
	#
	if is_linux; then
		log_must test -f $mntpt/file.$1
		log_must eval "[[ \$(cat $mntpt/file.$1) == $1 ]]"
	fi
	log_must zfs destroy -rf $child
	datasetexists $child && log_fail "$1: $child still exists"
	ismounted $child && log_fail "$1: $child still mounted after destroy"
	[[ -e $mntpt ]] && log_note "$1: $mntpt left behind"
	return 0
}

log_assert "Rollback of a parent keeps a mounted child usable and destroyable"
log_onexit cleanup

log_must zfs create $parent
log_must zfs snapshot $parent@before

# A child created after the snapshot, so its mountpoint is not in it.
for i in 1 2 3 4 5; do
	log_must zfs create $child
	ismounted $child || log_fail "$i: new $child is not mounted"
	log_must eval "echo $i > $(get_prop mountpoint $child)/file.$i"
	log_must zfs rollback $parent@before
	check_child $i
done

# A child that existed (with its mountpoint) when the snapshot was taken.
log_must zfs create $child
log_must zfs snapshot -r $parent@with_child
for i in 6 7 8; do
	log_must zfs create -p $child
	ismounted $child || log_fail "$i: new $child is not mounted"
	log_must eval "echo $i > $(get_prop mountpoint $child)/file.$i"
	log_must zfs rollback -r $parent@with_child
	check_child $i
done

log_pass "Rollback of a parent keeps a mounted child usable and destroyable"
