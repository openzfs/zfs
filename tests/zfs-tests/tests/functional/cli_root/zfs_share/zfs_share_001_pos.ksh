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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016, 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs set sharenfs' and 'zfs share' shares a given dataset.
#
# STRATEGY:
# 1. Invoke 'zfs set sharenfs'.
# 2. Verify that the file system is shared.
# 3. Invoke 'zfs share'.
# 4. Verify that the file system is shared.
# 5. Verify that a shared filesystem cannot be shared again.
# 6. Verify that share -a succeeds.
#

verify_runnable "global"

set -A fs \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1" \
    "$TESTDIR2" "$TESTPOOL/$TESTFS-clone" \
    "$TESTDIR" "$TESTPOOL/$TESTFS"

function cleanup
{
	typeset -i i=0
	while (( i < ${#fs[*]} )); do
		log_must zfs set sharenfs=off ${fs[((i+1))]}
		unshare_fs ${fs[i]}

		((i = i + 2))
	done

	if mounted $TESTPOOL/$TESTFS-clone; then
		log_must zfs unmount $TESTDIR2
	fi

	datasetexists $TESTPOOL/$TESTFS-clone && \
		destroy_dataset $TESTPOOL/$TESTFS-clone -f

	snapexists "$TESTPOOL/$TESTFS@snapshot" && \
		destroy_dataset $TESTPOOL/$TESTFS@snapshot -f

	log_must zfs share -a
}


#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# share the mountpoint and then verify it has been shared.
#
function test_share # mntp filesystem
{
	typeset mntp=$1
	typeset filesystem=$2

	not_shared $mntp || \
	    log_fail "File system $filesystem is already shared."

	log_must zfs set sharenfs=on $filesystem
	is_shared $mntp || \
	    log_fail "File system $filesystem is not shared (set sharenfs)."

	#
	# Verify 'zfs share' works as well.
	#
	log_must zfs unshare $filesystem
	is_shared $mntp && \
	    log_fail "File system $filesystem is still shared."

	log_must zfs share $filesystem
	is_shared $mntp || \
	    log_fail "file system $filesystem is not shared (zfs share)."

	log_note "Sharing a shared file system fails."
	log_mustnot zfs share $filesystem
}

log_assert "Verify that 'zfs share' succeeds as root."
log_onexit cleanup

log_must zfs snapshot $TESTPOOL/$TESTFS@snapshot
log_must zfs clone $TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTFS-clone
log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL/$TESTFS-clone

typeset -i i=0
while (( i < ${#fs[*]} )); do
	test_share ${fs[i]} ${fs[((i + 1))]}

	((i = i + 2))
done

log_note "Verify 'zfs share -a' succeeds."

#
# Unshare each of the file systems.
#
i=0
while (( i < ${#fs[*]} )); do
	unshare_fs ${fs[i]}

	((i = i + 2))
done

#
# Try a zfs share -a and verify all file systems are shared.
#
log_must zfs share -a

#
# We need to unset __ZFS_POOL_EXCLUDE so that we include all file systems
# in the os-specific zfs exports file. This will be reset by the next test.
#
unset __ZFS_POOL_EXCLUDE

i=0
while (( i < ${#fs[*]} )); do
	is_shared ${fs[i]} || \
	    log_fail "File system ${fs[i]} is not shared (share -a)"

	is_exported ${fs[i]} || \
	    log_fail "File system ${fs[i]} is not exported (share -a)"

	((i = i + 2))
done

log_pass "'zfs share [ -a ] <filesystem>' succeeds as root."
