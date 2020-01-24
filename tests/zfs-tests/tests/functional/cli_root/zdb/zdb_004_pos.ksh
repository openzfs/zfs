#!/bin/ksh

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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb will not produce redundant dumps of uberblocks
#
# Strategy:
# 1. Create a pool with two vdevs, A and B
# 2. Offline vdev A
# 3. Do some I/O
# 4. Export the pool
# 5. Copy label 1 from vdev A to vdev B
# 6. Collect zdb -lu output for vdev B
# 7. Verify labels 0 and 1 have unique Uberblocks, but 2 and 3 have none
#

log_assert "Verify zdb produces unique dumps of uberblocks"
log_onexit cleanup

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	for DISK in $DISKS; do
		zpool labelclear -f $DEV_RDSKDIR/$DISK
	done
	if is_freebsd; then
		log_must sysctl kern.geom.debugflags=$saved_debugflags
	fi
}

if is_freebsd; then
	# FreeBSD won't allow writing to an in-use device without this set
	saved_debugflags=$(sysctl -n kern.geom.debugflags)
	log_must sysctl kern.geom.debugflags=16
fi

verify_runnable "global"
verify_disk_count "$DISKS" 2
set -A DISK $DISKS
WHOLE_DISK=${DISK[0]}

default_mirror_setup_noexit $DISKS
DEVS=$(get_pool_devices ${TESTPOOL} ${DEV_RDSKDIR})
[[ -n $DEVS ]] && set -A DISK $DEVS

log_must zpool offline $TESTPOOL $WHOLE_DISK
log_must dd if=/dev/urandom of=$TESTDIR/testfile bs=1K count=2
log_must zpool export $TESTPOOL

log_must dd if=$DEV_RDSKDIR/${DISK[0]} of=$DEV_RDSKDIR/${DISK[1]} bs=1K count=256 conv=notrunc

ubs=$(zdb -lu ${DISK[1]} | grep -e LABEL -e Uberblock -e 'labels = ')
log_note "vdev 1: ubs $ubs"

ub_dump_counts=$(zdb -lu ${DISK[1]} | \
	awk '	/LABEL/	{label=$NF; blocks[label]=0};
		/Uberblock/ {blocks[label]++};
		END {print blocks[0],blocks[1],blocks[2],blocks[3]}')
(( $? != 0)) && log_fail "failed to get ub_dump_counts from DISK[1]"
log_note "vdev 1: ub_dump_counts $ub_dump_counts"

set -A dump_count $ub_dump_counts
for label in 0 1 2 3; do
	if [[ $label -lt 2 ]]; then
 		[[ ${dump_count[$label]} -eq 0 ]] && \
		    log_fail "zdb incorrectly dumps duplicate uberblocks"
	else
 		[[ ${dump_count[$label]} -ne 0 ]] && \
		    log_fail "zdb incorrectly dumps duplicate uberblocks"
	fi
done

cleanup

log_pass "zdb produces unique dumps of uberblocks"
