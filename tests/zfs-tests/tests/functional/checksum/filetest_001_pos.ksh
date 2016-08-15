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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib

# DESCRIPTION:
# Sanity test to make sure checksum algorithms work.
# For each checksum, create a file in the pool using that checksum.  Verify
# that there are no checksum errors.  Next, for each checksum, create a single
# file in the pool using that checksum, scramble the underlying vdev, and
# verify that we correctly catch the checksum errors.
#
# STRATEGY:
# Test 1
# 1. Create a mirrored pool
# 2. Create a file using each checksum
# 3. Export/import/scrub the pool
# 4. Verify there's no checksum errors.
# 5. Clear the pool
#
# Test 2
# 6. For each checksum:
# 7.	Create a file using the checksum
# 8.	Export the pool
# 9.	Scramble the data on one of the underlying VDEVs
# 10.	Import the pool
# 11.	Scrub the pool
# 12.	Verify that there are checksum errors

verify_runnable "both"

function cleanup
{
	$ECHO cleanup
	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Create and read back files with using different checksum algorithms"

log_onexit cleanup

FSSIZE=$($ZPOOL list -Hp -o size $TESTPOOL)
WRITESZ=1048576
WRITECNT=$((($FSSIZE) / $WRITESZ ))
# Skip the first and last 4MB
SKIP=4127518
SKIPCNT=$((($SKIP / $WRITESZ )))
SKIPCNT=$((($SKIPCNT * 2)))
WRITECNT=$((($WRITECNT - $SKIPCNT)))

# Get a list of vdevs in our pool
set -A array $(get_disklist_fullpath)

# Get the first vdev, since we will corrupt it later
firstvdev=${array[0]}

# First test each checksum by writing a file using it, and confirm there's no
# errors.
for ((count = 0; count < ${#checksum_props[*]} ; count++)); do
	i=${checksum_props[$count]}
	$ZFS set checksum=$i $TESTPOOL
	$FILE_WRITE -o overwrite -f $TESTDIR/test_$i -b $WRITESZ -c 5 -d R
done
$ZPOOL export $TESTPOOL
$ZPOOL import $TESTPOOL
$ZPOOL scrub $TESTPOOL
while is_pool_scrubbing $TESTPOOL; do
	$SLEEP 1
done
$ZPOOL status -P -v $TESTPOOL | grep $firstvdev | read -r name state rd wr cksum
log_assert "Normal file write test saw $cksum checksum errors"
log_must [ $cksum -eq 0 ]

rm -fr $TESTDIR/*

log_assert "Test scrambling the disk and seeing checksum errors"
for ((count = 0; count < ${#checksum_props[*]} ; count++)); do
	i=${checksum_props[$count]}
	$ZFS set checksum=$i $TESTPOOL
	$FILE_WRITE -o overwrite -f $TESTDIR/test_$i -b $WRITESZ -c 5 -d R

	$ZPOOL export $TESTPOOL

	# Scramble the data on the first vdev in our pool.
	# Skip the first and last 16MB of data, then scramble the rest after that
	#
	$FILE_WRITE -o overwrite -f $firstvdev -s $SKIP -c $WRITECNT -b $WRITESZ -d R

	$ZPOOL import $TESTPOOL

	i=${checksum_props[$count]}
	$ZPOOL scrub $TESTPOOL
	while is_pool_scrubbing $TESTPOOL; do
                $SLEEP 1
        done

	$ZPOOL status -P -v $TESTPOOL | grep $firstvdev | read -r name state rd wr cksum

	log_assert "Checksum '$i' caught $cksum checksum errors"
	log_must [ $cksum -ne 0 ]

	rm -f $TESTDIR/test_$i
	$ZPOOL clear $TESTPOOL
done
