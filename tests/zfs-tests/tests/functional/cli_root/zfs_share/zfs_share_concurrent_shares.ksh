#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs set sharenfs=on', 'zfs share', and 'zfs unshare' can
# run concurrently. The test creates 50 filesystem and 50 threads.
# Each thread will run through the test strategy in parallel.
#
# STRATEGY:
# 1. Verify that the file system is not shared.
# 2. Enable the 'sharenfs' property
# 3. Invoke 'zfs unshare' and verify filesystem is no longer shared
# 4. Invoke 'zfs share'.
# 4. Verify that the file system is shared.
# 5. Verify that a shared filesystem cannot be shared again.
# 6. Verify that share -a succeeds.
#

verify_runnable "global"

function cleanup
{
	wait
	for fs in {0..50}
	do
		for pfs in $TESTFS1 $TESTFS2 $TESTFS3
		do
			log_must zfs set sharenfs=off $TESTPOOL/$pfs/$fs
			unshare_fs $TESTPOOL/$pfs/$fs

			if mounted $TESTPOOL/$pfs/$fs; then
				log_must zfs unmount $TESTPOOL/$pfs/$fs
			fi

			datasetexists $TESTPOOL/$pfs/$fs && \
				destroy_dataset $TESTPOOL/$pfs/$fs -f
		done
	done

	log_must zfs share -a
}

function create_filesystems
{
	for fs in {0..50}
	do
		log_must zfs create -p $TESTPOOL/$TESTFS1/$fs
		log_must zfs create -p $TESTPOOL/$TESTFS2/$fs
		log_must zfs create -p $TESTPOOL/$TESTFS3/$fs
	done
}

function sub_fail
{
	log_note $$: "$@"
	exit 1
}

#
# Main test routine.
#
# Given a file system this routine will attempt
# share the mountpoint and then verify it has been shared.
#
function test_share # filesystem
{
	typeset filesystem=$1
	typeset mntp=$(get_prop mountpoint $filesystem)

	not_shared $mntp || \
	    sub_fail "File system $filesystem is already shared."

	zfs set sharenfs=on $filesystem || \
	    sub_fail "zfs set sharenfs=on $filesystem failed."
	is_shared $mntp || \
	    sub_fail "File system $filesystem is not shared (set sharenfs)."

	#
	# Verify 'zfs share' works as well.
	#
	zfs unshare $filesystem || \
	    sub_fail "zfs unshare $filesystem failed."
	is_shared $mntp && \
	    sub_fail "File system $filesystem is still shared."


	zfs share $filesystem || \
	    sub_fail "zfs share $filesystem failed."
	is_shared $mntp || \
	    sub_fail "file system $filesystem is not shared (zfs share)."


	#log_note "Sharing a shared file system fails."
	zfs share $filesystem && \
	    sub_fail "zfs share $filesystem did not fail"

	return 0
}

function unshare_fs_nolog
{
	typeset fs=$1

	if is_shared $fs || is_shared_smb $fs; then
		zfs unshare $fs ||
		    sub_fail "zfs unshare $fs: $?"
	fi
}

#
# Set the main process id so that we know to capture
# failures from child processes and allow the parent process
# to report the failure.
#
set_main_pid $$
log_assert "Verify that 'zfs share' succeeds as root."
log_onexit cleanup

create_filesystems

child_pids=()
for fs in {0..50}
do
	for pfs in $TESTFS1 $TESTFS2 $TESTFS3
	do
		test_share $TESTPOOL/$pfs/$fs &
		child_pids+=($!)
		log_note "$TESTPOOL/$pfs/$fs ==> $!"
	done
done
log_must wait_for_children "${child_pids[@]}"

log_note "Verify 'zfs share -a' succeeds."

#
# Unshare each of the file systems.
#
child_pids=()
for fs in {0..50}
do
	for pfs in $TESTFS1 $TESTFS2 $TESTFS3
	do
		unshare_fs_nolog $TESTPOOL/$pfs/$fs &
		child_pids+=($!)
		log_note "$TESTPOOL/$pfs/$fs (unshare) ==> $!"
	done
done
log_must wait_for_children "${child_pids[@]}"

#
# Try a zfs share -a and verify all file systems are shared.
#
log_must zfs share -a

#
# We need to unset __ZFS_POOL_EXCLUDE so that we include all file systems
# in the os-specific zfs exports file. This will be reset by the next test.
#
unset __ZFS_POOL_EXCLUDE

for fs in {0..50}
do
	for pfs in $TESTFS1 $TESTFS2 $TESTFS3
	do
		log_must is_shared $TESTPOOL/$pfs/$fs
		log_must is_exported $TESTPOOL/$pfs/$fs
	done
done

log_pass "'zfs share [-a] <filesystem>' succeeds as root."
