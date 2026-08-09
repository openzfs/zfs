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

#
# Copyright (c) 2024 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify checksum verify works for Direct I/O reads.
#
# STRATEGY:
#	1. Create a zpool from each vdev type.
#	2. Start a Direct I/O read workload while manipulating the user buffer
#	   contents.
#	3. Verify there are Direct I/O read verify failures using
#	   zpool status -d and checking for zevents. We also make sure there
#	   are reported no data errors.
#

verify_runnable "global"

log_assert "Verify checksum verify works for Direct I/O reads."

log_onexit dio_cleanup

NUMBLOCKS=300
BS=$((128 * 1024)) # 128k

log_must  truncate -s $MINVDEVSIZE $DIO_VDEVS

# We will verify that there are no checksum errors for every Direct I/O read
# while manipulating the buffer contents while the I/O is still in flight and
# also that Direct I/O checksum verify failures and dio_verify_rd zevents are
# reported.
for type in "" "mirror" "raidz" "draid"; do
	typeset vdev_type=$type
	if [[ "${vdev_type}" == "" ]]; then
		vdev_type="stripe"
	fi

	log_note "Verifying every Direct I/O read verify with VDEV type \
	    ${vdev_type}"

	create_pool $TESTPOOL1 $type $DIO_VDEVS
	log_must eval "zfs create -o recordsize=128k -o compression=off  \
	    $TESTPOOL1/$TESTFS1"

	mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)
	prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
	prev_arc_rd=$(kstat_pool $TESTPOOL1 iostats.arc_read_count)

	# Create the file before trying to manipulate the contents
	log_must stride_dd -o "$mntpnt/direct-write.iso" -i /dev/urandom \
	    -b $BS -c $NUMBLOCKS -D
	# Manipulate the buffer contents will reading the file with Direct I/O
	log_must manipulate_user_buffer -f "$mntpnt/direct-write.iso" \
	    -n $NUMBLOCKS -b $BS -r

	# Getting new Direct I/O and ARC read counts.
	curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
	curr_arc_rd=$(kstat_pool $TESTPOOL1 iostats.arc_read_count)
	total_dio_rd=$((curr_dio_rd - prev_dio_rd))
	total_arc_rd=$((curr_arc_rd - prev_arc_rd))

	log_note "Making sure we have Direct I/O reads logged"
	if [[ $total_dio_rd -lt 1 ]]; then
		log_fail "No Direct I/O reads $total_dio_rd"
	fi

	log_note "Making sure we have Direct I/O read checksum verifies with ZPool"
	check_dio_chksum_verify_failures "$TESTPOOL1" "$vdev_type" 1 "rd"

	log_note "Making sure we have ARC reads logged"
	if [[ $total_arc_rd -lt 1 ]]; then
		log_fail "No ARC reads $total_arc_rd"
	fi

	log_note "Making sure there are no checksum errors with the ZPool"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"	

	destroy_pool $TESTPOOL1
done

log_pass "Verified checksum verify works for Direct I/O reads." 
