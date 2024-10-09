#!/bin/ksh -p
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
# Copyright (c) 2022 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify checksum verify works for Direct I/O writes.
#
# STRATEGY:
#	1. Set the module parameter zfs_vdev_direct_write_verify to 0.
#	2. Check that manipulating the user buffer while Direct I/O writes are
#	   taking place does not cause any panics with compression turned on.
#	3. Start a Direct I/O write workload while manipulating the user buffer
#	   without compression.
#	4. Verify there are Direct I/O write verify failures using
#	   zpool status -d and checking for zevents. We also make sure there
#	   are reported data errors when reading the file back.
#	5. Repeat steps 3 and 4 for 3 iterations.
#	6. Set zfs_vdev_direct_write_verify set to 1 and repeat 3.
#	7. Verify there are Direct I/O write verify failures using
#	   zpool status -d and checking for zevents. We also make sure there
#	   there are no reported data errors when reading the file back because
#	   with us checking every Direct I/O write and on checksum validation
#	   failure those writes will not be committed to a VDEV.
#

verify_runnable "global"

function cleanup
{
	# Clearing out DIO counts for Zpool
	log_must zpool clear $TESTPOOL
	# Clearing out dio_verify from event logs
	log_must zpool events -c
	log_must set_tunable32 VDEV_DIRECT_WR_VERIFY $DIO_WR_VERIFY_TUNABLE
}

log_assert "Verify checksum verify works for Direct I/O writes."

if is_freebsd; then
	log_unsupported "FreeBSD is capable of stable pages for O_DIRECT writes"
fi

log_onexit cleanup

ITERATIONS=3
NUMBLOCKS=300
BS=$((128 * 1024)) # 128k
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset DIO_WR_VERIFY_TUNABLE=$(get_tunable VDEV_DIRECT_WR_VERIFY)

# Get a list of vdevs in our pool
set -A array $(get_disklist_fullpath $TESTPOOL)

# Get the first vdev
firstvdev=${array[0]}

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
log_must set_tunable32 VDEV_DIRECT_WR_VERIFY 0

# First we will verify there are no panics while manipulating the contents of
# the user buffer during Direct I/O writes with compression. The contents
# will always be copied out of the ABD and there should never be any ABD ASSERT
# failures
log_note "Verifying no panics for Direct I/O writes with compression"
log_must zfs set compression=on $TESTPOOL/$TESTFS
prev_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
log_must manipulate_user_buffer -f "$mntpnt/direct-write.iso" -n $NUMBLOCKS \
    -b $BS -w
curr_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
total_dio_wr=$((curr_dio_wr - prev_dio_wr))

log_note "Making sure we have Direct I/O writes logged"
if [[ $total_dio_wr -lt 1 ]]; then
	log_fail "No Direct I/O writes $total_dio_wr"
fi

# Clearing out DIO counts for Zpool
log_must zpool clear $TESTPOOL
# Clearing out dio_verify from event logs
log_must zpool events -c
log_must rm -f "$mntpnt/direct-write.iso"

# Next we will verify there are checksum errors for Direct I/O writes while
# manipulating the contents of the user pages.
log_must zfs set compression=off $TESTPOOL/$TESTFS

for i in $(seq 1 $ITERATIONS); do
	log_note "Verifying Direct I/O write checksums iteration \
	    $i of $ITERATIONS with zfs_vdev_direct_write_verify=0"

	prev_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
	log_must manipulate_user_buffer -f "$mntpnt/direct-write.iso" \
	    -n $NUMBLOCKS -b $BS -w

	# Reading file back to verify checksum errors
	filesize=$(get_file_size "$mntpnt/direct-write.iso")
	num_blocks=$((filesize / BS))
	log_mustnot stride_dd -i "$mntpnt/direct-write.iso" -o /dev/null -b $BS \
	    -c $num_blocks

	# Getting new Direct I/O and ARC write counts.
	curr_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
	total_dio_wr=$((curr_dio_wr - prev_dio_wr))

	# Verifying there are checksum errors
	log_note "Making sure there are checksum errors for the ZPool"
	cksum=$(zpool status -P -v $TESTPOOL | awk -v v="$firstvdev" '$0 ~ v \
	    {print $5}')
	if  [[ $cksum -eq 0 ]]; then
		zpool status -P -v $TESTPOOL
		log_fail "No checksum failures for ZPool $TESTPOOL"
	fi
	
	log_note "Making sure we have Direct I/O writes logged"
	if [[ $total_dio_wr -lt 1 ]]; then
		log_fail "No Direct I/O writes $total_dio_wr"
	fi
	log_note "Making sure we have no Direct I/O write checksum verifies \
	    with ZPool"
	check_dio_chksum_verify_failures $TESTPOOL "raidz" 0 "wr"

	log_must rm -f "$mntpnt/direct-write.iso"
done

log_must zpool status -v $TESTPOOL
log_must zpool sync $TESTPOOL



# Finally we will verfiy that with checking every Direct I/O write we have no
# errors at all.
# Create the file before trying to manipulate the contents
log_must file_write -o create -f "$mntpnt/direct-write.iso" -b $BS \
    -c $NUMBLOCKS -w
log_must set_tunable32 VDEV_DIRECT_WR_VERIFY 1

for i in $(seq 1 $ITERATIONS); do
	log_note "Verifying every Direct I/O write checksums iteration $i of \
	    $ITERATIONS with zfs_vdev_direct_write_verify=1"

	prev_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
	log_must manipulate_user_buffer -f "$mntpnt/direct-write.iso" \
	    -n $NUMBLOCKS -b $BS -e -w

	# Reading file back to verify there no are checksum errors
	filesize=$(get_file_size "$mntpnt/direct-write.iso")
	num_blocks=$((filesize / BS))
	log_must stride_dd -i "$mntpnt/direct-write.iso" -o /dev/null -b $BS \
	    -c $num_blocks

	# Getting new Direct I/O write counts.
	curr_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
	total_dio_wr=$((curr_dio_wr - prev_dio_wr))

	log_note "Making sure there are no checksum errors with the ZPool"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	log_note "Making sure we have Direct I/O writes logged"
	if [[ $total_dio_wr -lt 1 ]]; then
		log_fail "No Direct I/O writes $total_dio_wr"
	fi

	log_note "Making sure we have Direct I/O write checksum verifies with ZPool"
	check_dio_chksum_verify_failures "$TESTPOOL" "raidz" 1 "wr"
done

log_must rm -f "$mntpnt/direct-write.iso"

log_pass "Verified checksum verify works for Direct I/O writes." 
