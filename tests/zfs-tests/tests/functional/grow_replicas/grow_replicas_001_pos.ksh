#! /bin/ksh -p
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/grow_replicas/grow_replicas.cfg

# DESCRIPTION:
# A ZFS filesystem is limited by the amount of disk space
# available to the pool. Growing the pool by adding a disk
# increases the amount of space.
#
# STRATEGY:
# 1. Fill the filesystem on mirror/raidz pool by writing a file until ENOSPC.
# 2. Grow the mirror/raidz pool by adding another mirror/raidz vdev.
# 3. Verify that more data can now be written to the filesystem.

verify_runnable "global"

if is_32bit; then
	log_unsupported "Test case fails on 32-bit systems"
fi

if ! is_physical_device $DISKS; then
	log_unsupported "This test case cannot be run on raw files"
fi

function cleanup
{
	datasetexists $TESTPOOL && log_must destroy_pool $TESTPOOL
	[[ -d $TESTDIR ]] && log_must rm -rf $TESTDIR
}

log_assert "mirror/raidz pool may be increased in capacity by adding a disk"

log_onexit cleanup

readonly ENOSPC=28

for pooltype in "mirror" "raidz"; do
	log_note "Creating pool type: $pooltype"

	if [[ -n $DISK ]]; then
		log_note "No spare disks available. Using slices on $DISK"
		for slice in $SLICES; do
			log_must set_partition $slice "$cyl" $SIZE $DISK
			cyl=$(get_endslice $DISK $slice)
		done
		create_pool $TESTPOOL $pooltype \
			${DISK}${SLICE_PREFIX}${SLICE0} \
			${DISK}${SLICE_PREFIX}${SLICE1}
	else
		log_must set_partition 0 "" $SIZE $DISK0
		log_must set_partition 0 "" $SIZE $DISK1
		create_pool $TESTPOOL $pooltype \
			${DISK0}${SLICE_PREFIX}${SLICE0} \
			${DISK1}${SLICE_PREFIX}${SLICE0}
	fi

	[[ -d $TESTDIR ]] && log_must rm -rf $TESTDIR
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	log_must zfs set compression=off $TESTPOOL/$TESTFS
	file_write -o create -f $TESTDIR/$TESTFILE1 \
            -b $BLOCK_SIZE -c $WRITE_COUNT -d 0

	[[ $? -ne $ENOSPC ]] && \
	    log_fail "file_write completed w/o ENOSPC"

	[[ ! -s $TESTDIR/$TESTFILE1 ]] && \
	    log_fail "$TESTDIR/$TESTFILE1 was not created"

	# $DISK will be set if we're using slices on one disk
	if [[ -n $DISK ]]; then
		log_must zpool add $TESTPOOL $pooltype \
		    ${DISK}${SLICE_PREFIX}${SLICE3} \
		    ${DISK}${SLICE_PREFIX}${SLICE4}
	else
		[[ -z $DISK2 || -z $DISK3 ]] && 
		    log_unsupported "No spare disks available"
		log_must zpool add $TESTPOOL $pooltype \
			${DISK2}${SLICE_PREFIX}${SLICE0} \
			${DISK3}${SLICE_PREFIX}${SLICE0}
	fi

	log_must file_write -o append -f $TESTDIR/$TESTFILE1 \
	    -b $BLOCK_SIZE -c $SMALL_WRITE_COUNT -d 0

	log_must destroy_pool $TESTPOOL
done

log_pass "mirror/raidz pool successfully grown"
