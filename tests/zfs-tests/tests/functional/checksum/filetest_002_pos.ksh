#! /bin/ksh -p
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
# Copyright (c) 2018, 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/checksum/default.cfg

# DESCRIPTION:
# Sanity test to make sure checksum algorithms work.
# For each checksum, create a file in the pool using that checksum.  Verify
# that there are no checksum errors.  Next, for each checksum, create a single
# file in the pool using that checksum, corrupt the file, and verify that we
# correctly catch the checksum errors.
#
# STRATEGY:
# Test 1
# 1. For each checksum:
# 2.	Create a file using the checksum
# 3.	Corrupt all level 1 blocks in the file
# 4.	Export and import the pool
# 5.	Verify that there are checksum errors

verify_runnable "both"

function cleanup
{
	rm -fr $TESTDIR/*
}

log_assert "Test corrupting files at L1 and seeing checksum errors"

log_onexit cleanup

WRITESZ=1048576
NWRITES=5

# Get a list of vdevs in our pool
set -A array $(get_disklist_fullpath)

# Get the first vdev, since we will corrupt it later
firstvdev=${array[0]}

typeset -i j=1
while [[ $j -lt ${#CHECKSUM_TYPES[*]} ]]; do
	type=${CHECKSUM_TYPES[$j]}
	log_must zfs set checksum=$type $TESTPOOL
	log_must file_write -o overwrite -f $TESTDIR/test_$type \
	    -b $WRITESZ -c $NWRITES -d R

	# Corrupt the level 1 blocks of this file
	corrupt_blocks_at_level $TESTDIR/test_$type 1

	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL

	log_mustnot eval "cat $TESTDIR/test_$type >/dev/null"

	cksum=$(zpool status -P -v $TESTPOOL | grep "$firstvdev" | \
	    awk '{print $5}')

	log_assert "Checksum '$type' caught $cksum checksum errors"
	log_must [ $cksum -ne 0 ]

	rm -f $TESTDIR/test_$type
	log_must zpool clear $TESTPOOL

	(( j = j + 1 ))
done
