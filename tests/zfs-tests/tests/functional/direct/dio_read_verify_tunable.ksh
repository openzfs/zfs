#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2026, Michael Heller <michael.heller@gmail.com>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
#	Verify the zfs_vdev_direct_read_verify module parameter controls
#	Direct I/O read checksum verification for every vdev type.
#
# STRATEGY:
#	1. Create a zpool from each vdev type.
#	2. With zfs_vdev_direct_read_verify=0, start a Direct I/O read
#	   workload while manipulating the user buffer contents.  Verify there
#	   are NO Direct I/O read verify failures reported (the knob disables
#	   the verify), and no data errors.
#	3. With zfs_vdev_direct_read_verify=1, repeat the workload and verify
#	   there ARE Direct I/O read verify failures reported (the default
#	   behavior), and still no data errors.
#

verify_runnable "global"

function cleanup
{
	dio_cleanup
	log_must set_tunable32 VDEV_DIRECT_RD_VERIFY $DIO_RD_VERIFY_TUNABLE
}

log_assert "Verify zfs_vdev_direct_read_verify controls Direct I/O read " \
    "checksum verification."

log_onexit cleanup

NUMBLOCKS=300
BS=$((128 * 1024)) # 128k
typeset DIO_RD_VERIFY_TUNABLE=$(get_tunable VDEV_DIRECT_RD_VERIFY)

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS

for type in "" "mirror" "raidz" "draid"; do
	typeset vdev_type=$type
	if [[ "${vdev_type}" == "" ]]; then
		vdev_type="stripe"
	fi

	log_note "Testing zfs_vdev_direct_read_verify with VDEV type ${vdev_type}"

	create_pool $TESTPOOL1 $type $DIO_VDEVS
	log_must eval "zfs create -o recordsize=128k -o compression=off  \
	    $TESTPOOL1/$TESTFS1"
	mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

	# Create the file before manipulating its contents during reads.
	log_must stride_dd -o "$mntpnt/direct-read.iso" -i /dev/urandom \
	    -b $BS -c $NUMBLOCKS -D

	#
	# With verification disabled, manipulating the user buffer while a
	# Direct I/O read is in flight must not produce any dio_verify_rd
	# events, and the pool must remain error-free (the on-disk data is
	# correct).
	#
	log_must set_tunable32 VDEV_DIRECT_RD_VERIFY 0
	log_must zpool clear $TESTPOOL1
	log_must zpool events -c

	prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
	log_must manipulate_user_buffer -f "$mntpnt/direct-read.iso" \
	    -n $NUMBLOCKS -b $BS -r
	curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
	total_dio_rd=$((curr_dio_rd - prev_dio_rd))

	log_note "Making sure we have Direct I/O reads logged"
	if [[ $total_dio_rd -lt 1 ]]; then
		log_fail "No Direct I/O reads $total_dio_rd"
	fi

	log_note "Making sure there are no Direct I/O read verify failures"
	check_dio_chksum_verify_failures "$TESTPOOL1" "$vdev_type" 0 "rd"

	log_note "Making sure there are no checksum errors with the ZPool"
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

	#
	# With verification enabled (the default), the same workload must
	# produce dio_verify_rd events, while the pool stays error-free.
	#
	log_must set_tunable32 VDEV_DIRECT_RD_VERIFY 1
	log_must zpool clear $TESTPOOL1
	log_must zpool events -c

	log_must manipulate_user_buffer -f "$mntpnt/direct-read.iso" \
	    -n $NUMBLOCKS -b $BS -r

	log_note "Making sure we have Direct I/O read verify failures"
	check_dio_chksum_verify_failures "$TESTPOOL1" "$vdev_type" 1 "rd"

	log_note "Making sure there are no checksum errors with the ZPool"
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

	destroy_pool $TESTPOOL1
done

log_pass "Verified zfs_vdev_direct_read_verify controls Direct I/O read " \
    "checksum verification."
