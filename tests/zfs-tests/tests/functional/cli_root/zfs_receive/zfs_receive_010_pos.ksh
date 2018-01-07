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
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test that receiving a full send as a clone works correctly.
#
# STRATEGY:
# 1. Create pool and filesystems.
# 2. Send filesystem, receive as clone of itself.
# 3. Verify that nop-write saves space.
# 4. Send filesystem, receive as clone of other filesystem.
# 5. Verify that contents are correct.
# 6. Repeat steps 4 and 5 with filesystems swapped.
#

verify_runnable "both"

fs=$TESTPOOL/$TESTFS/base/fs
fs2=$TESTPOOL/$TESTFS/base/fs2
rfs=$TESTPOOL/$TESTFS/base/rfs

function make_object
{
	typeset objnum=$1
	typeset mntpnt=$2
	typeset type=$3
	if [[ $type == "file" ]]; then
		dd if=/dev/urandom of=${mntpnt}/f$objnum bs=512 count=16
	elif [[ $type == "hole1" ]]; then
		dd if=/dev/zero of=${mntpnt}/fh$objnum bs=512 count=5 seek=4 conv=notrunc
	elif [[ $type == "hole2" ]]; then
		dd if=/dev/zero of=${mntpnt}/fh$objnum bs=512 count=4 seek=5 conv=notrunc
	elif [[ $type == "directory" ]]; then
		mkdir ${mntpnt}/d$objnum
	elif [[ $type == "missing" ]]; then
		touch ${mntpnt}/h$objnum
	fi
}

function create_pair
{
	typeset objnum=$1
	typeset mntpnt1=$2
	typeset mntpnt2=$3
	typeset type1=$4
	typeset type2=$5
	make_object $objnum $mntpnt1 $type1
	make_object $objnum $mntpnt2 $type2
}

function cleanup
{
	zfs destroy -Rf $TESTPOOL/$TESTFS/base
	rm $TESTDIR/zr010p*
}

log_assert "zfs receive of full send as clone should work"
log_onexit cleanup
log_must zfs create -o checksum=sha256 -o compression=gzip -o recordsize=512 \
	$TESTPOOL/$TESTFS/base

log_must zfs create $fs
log_must zfs create $fs2
mntpnt=$(get_prop mountpoint $fs)
mntpnt2=$(get_prop mountpoint $fs2)

#
# Now, we create the two filesystems.  By creating objects with
# different types and the same object number in each filesystem, we
# create a situation where, when you receive the full send of each as
# a clone of the other, we will test to ensure that the code correctly
# handles receiving all object types onto all other object types.
#

# Receive a file onto a file (and vice versa).
create_pair 8 $mntpnt $mntpnt2 "file" "file"

# Receive a file onto a file with holes (and vice versa).
create_pair 9 $mntpnt $mntpnt2 "file" "hole1"

# Receive a file onto a directory (and vice versa).
create_pair 10 $mntpnt $mntpnt2 "file" "directory"

# Receive a file onto a missing object (and vice versa).
create_pair 11 $mntpnt $mntpnt2 "file" "missing"

# Receive a file with holes onto a file with holes (and vice versa).
create_pair 12 $mntpnt $mntpnt2 "hole1" "hole2"

# Receive a file with holes onto a directory (and vice versa).
create_pair 13 $mntpnt $mntpnt2 "hole1" "directory"

# Receive a file with holes onto a missing object (and vice versa).
create_pair 14 $mntpnt $mntpnt2 "hole1" "missing"

# Receive a directory onto a directory (and vice versa).
create_pair 15 $mntpnt $mntpnt2 "directory" "directory"

# Receive a directory onto a missing object (and vice versa).
create_pair 16 $mntpnt $mntpnt2 "directory" "missing"

# Receive a missing object onto a missing object (and vice versa).
create_pair 17 $mntpnt $mntpnt2 "missing" "missing"

# Receive a file with a different record size onto a file (and vice versa).
log_must zfs set recordsize=128k $fs
dd if=/dev/urandom of=$mntpnt/f18 bs=128k count=64
touch $mntpnt2/f18

# Remove objects that are intended to be missing.
rm $mntpnt/h17
rm $mntpnt2/h*

# Add empty objects to $fs to exercise dmu_traverse code
for i in {1..100}; do
	log_must touch $mntpnt/uf$i
done

log_must zfs snapshot $fs@s1
log_must zfs snapshot $fs2@s1

log_must zfs send $fs@s1 > $TESTDIR/zr010p
log_must zfs send $fs2@s1 > $TESTDIR/zr010p2


#
# Test that, when we receive a full send as a clone of itself,
# nop-write saves us all the space used by data blocks.
#
cat $TESTDIR/zr010p | log_must zfs receive -o origin=$fs@s1 $rfs
size=$(get_prop used $rfs)
size2=$(get_prop used $fs)
if [[ $size -ge $(($size2 / 10)) ]] then
        log_fail "nop-write failure; expected usage less than "\
		"$(($size2 / 10)), but is using $size"
fi
log_must zfs destroy -fr $rfs

# Correctness testing: receive each full send as a clone of the other fiesystem.
cat $TESTDIR/zr010p | log_must zfs receive -o origin=$fs2@s1 $rfs
mntpnt_old=$(get_prop mountpoint $fs)
mntpnt_new=$(get_prop mountpoint $rfs)
log_must diff -r $mntpnt_old $mntpnt_new
log_must zfs destroy -r $rfs

cat $TESTDIR/zr010p2 | log_must zfs receive -o origin=$fs@s1 $rfs
mntpnt_old=$(get_prop mountpoint $fs2)
mntpnt_new=$(get_prop mountpoint $rfs)
log_must diff -r $mntpnt_old $mntpnt_new

log_pass "zfs receive of full send as clone works"
