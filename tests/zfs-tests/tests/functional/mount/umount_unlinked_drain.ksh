#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test async unlinked drain to ensure mounting is not held up when there are
# entries in the unlinked set. We also try to test that the list is able to be
# filled up and drained at the same time.
#
# STRATEGY:
# 1. Use zfs_unlink_suspend_progress tunable to disable freeing to build up
#    the unlinked set
# 2. Make sure mount happens even when there are entries in the unlinked set
# 3. Drain and build up the unlinked list at the same time to test for races
#

function cleanup
{
	log_must set_tunable32 UNLINK_SUSPEND_PROGRESS $default_unlink_sp
	for fs in $(seq 1 3); do
		mounted $TESTDIR.$fs || zfs mount $TESTPOOL/$TESTFS.$fs
		rm -f $TESTDIR.$fs/file-*
		zfs set xattr=on $TESTPOOL/$TESTFS.$fs
	done
}

function unlinked_size_is
{
	MAX_ITERS=5 # iteration to do before we consider reported number stable
	iters=0
	last_usize=0
	while [[ $iters -le $MAX_ITERS ]]; do
		kstat_file=$(grep -nrwl /proc/spl/kstat/zfs/$2/objset-0x* -e $3)
		nunlinks=`cat $kstat_file | grep nunlinks | awk '{print $3}'`
		nunlinked=`cat $kstat_file | grep nunlinked | awk '{print $3}'`
		usize=$(($nunlinks - $nunlinked))
		if [[ $iters == $MAX_ITERS && $usize == $1 ]]; then
			return 0
		fi
		if [[ $usize == $last_usize ]]; then
			(( iters++ ))
		else
			iters=0
		fi
		last_usize=$usize
	done

	log_note "Unexpected unlinked set size: $last_usize, expected $1"
	return 1
}


default_unlink_sp=$(get_tunable UNLINK_SUSPEND_PROGRESS)

log_onexit cleanup

log_assert "Unlinked list drain does not hold up mounting of fs"

for fs in 1 2 3; do
	set -A xattrs on sa off
	for xa in ${xattrs[@]}; do
		# setup fs and ensure all deleted files got into unliked set
		log_must mounted $TESTDIR.$fs

		log_must zfs set xattr=$xa $TESTPOOL/$TESTFS.$fs

		if [[ $xa == off ]]; then
			for fn in $(seq 1 175); do
				log_must mkfile 128k $TESTDIR.$fs/file-$fn
			done
		else
			log_must xattrtest -f 175 -x 3 -r -k -p $TESTDIR.$fs
		fi

		log_must set_tunable32 UNLINK_SUSPEND_PROGRESS 1
		log_must unlinked_size_is 0 $TESTPOOL $TESTPOOL/$TESTFS.$fs

		# build up unlinked set
		for fn in $(seq 1 100); do
			log_must eval "rm $TESTDIR.$fs/file-$fn &"
		done
		log_must unlinked_size_is 100 $TESTPOOL $TESTPOOL/$TESTFS.$fs

		# test that we can mount fs without emptying the unlinked list
		log_must zfs umount $TESTPOOL/$TESTFS.$fs
		log_must unmounted $TESTDIR.$fs
		log_must zfs mount $TESTPOOL/$TESTFS.$fs
		log_must mounted $TESTDIR.$fs
		log_must unlinked_size_is 100 $TESTPOOL $TESTPOOL/$TESTFS.$fs

		# confirm we can drain and add to unlinked set at the same time
		log_must set_tunable32 UNLINK_SUSPEND_PROGRESS 0
		log_must zfs umount $TESTPOOL/$TESTFS.$fs
		log_must zfs mount $TESTPOOL/$TESTFS.$fs
		for fn in $(seq 101 175); do
			log_must eval "rm $TESTDIR.$fs/file-$fn &"
		done
		log_must unlinked_size_is 0 $TESTPOOL $TESTPOOL/$TESTFS.$fs
	done
done

log_pass "Confirmed unlinked list drain does not hold up mounting of fs"
