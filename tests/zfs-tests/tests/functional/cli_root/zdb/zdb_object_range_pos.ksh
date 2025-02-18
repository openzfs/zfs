#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#
# Copyright (c) 2020 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Object range parameters passed to zdb -dd work correctly.
#
# Strategy:
# 1. Create a pool
# 2. Create some files
# 3. Run zdb -dd with assorted object range arguments and verify output

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

#
# Print objects in @dataset with identifiers greater than or equal to
# @begin and less than or equal to @end, without using object range
# parameters.
#
function get_object_list_range
{
	dataset=$1
	begin=$2
	end=$3
	get_object_list $dataset |
	while read -r line; do
		read -r obj _ <<<"$line"
		if [[ $obj -ge $begin && $obj -le $end ]] ; then
			echo "$line"
		elif [[ $obj -gt $end ]] ; then
			break
		fi
	done
}

#
# Print just the list of objects from 'zdb -dd' with leading whitespace
# trimmed, discarding other zdb output, sorted by object identifier.
# Caller must pass in the dataset argument at minimum.
#
function get_object_list
{
	zdb -P -dd $@ 2>/dev/null |
	sed -E '/^ +-?([0-9]+ +){7}/!d;s/^[[:space:]]*//' |
	sort -n
}

log_assert "Verify zdb -dd object range arguments work correctly."
log_onexit cleanup
verify_runnable "both"
verify_disk_count "$DISKS" 2
default_mirror_setup_noexit $DISKS

for x in $(seq 0 7); do
	touch $TESTDIR/file$x
	mkdir $TESTDIR/dir$x
done

sync_all_pools

# Get list of all objects, but filter out user/group objects which don't
# appear when using object or object range arguments
all_objects=$(get_object_list $TESTPOOL/$TESTFS | grep -v 'used$')

# Range 0:-1 gets all objects
expected=$all_objects
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:A gets all objects
expected=$all_objects
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1:A)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:f must output all file objects
expected=$(grep "ZFS plain file" <<< $all_objects)
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1:f)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:d must output all directory objects
expected=$(grep "ZFS directory" <<< $all_objects)
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1:d)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:df must output all directory and file objects
expected=$(grep -e "ZFS directory" -e "ZFS plain file" <<< $all_objects)
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1:df)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:A-f-d must output all non-files and non-directories
expected=$(grep -v -e "ZFS plain file" -e "ZFS directory" <<< $all_objects)
actual=$(get_object_list $TESTPOOL/$TESTFS 0:-1:A-f-d)
log_must test "\n$actual\n" == "\n$expected\n"

# Specifying multiple ranges works
set -A obj_ids $(ls -i $TESTDIR | awk '{print $1}' | sort -n)
start1=${obj_ids[0]}
end1=${obj_ids[5]}
start2=${obj_ids[8]}
end2=${obj_ids[13]}
expected=$(get_object_list_range $TESTPOOL/$TESTFS $start1 $end1;
    get_object_list_range $TESTPOOL/$TESTFS $start2 $end2)
actual=$(get_object_list $TESTPOOL/$TESTFS $start1:$end1 $start2:$end2)
log_must test "\n$actual\n" == "\n$expected\n"

# Combining ranges with individual object IDs works
expected=$(get_object_list_range $TESTPOOL/$TESTFS $start1 $end1;
    get_object_list $TESTPOOL/$TESTFS $start2 $end2)
actual=$(get_object_list $TESTPOOL/$TESTFS $start1:$end1 $start2 $end2)
log_must test "\n$actual\n" == "\n$expected\n"

# Hex conversion must work for ranges and individual object identifiers
# (this test uses expected result from previous test).
start1_hex=$(printf "0x%x" $start1)
end1_hex=$(printf "0x%x" $end1)
start2_hex=$(printf "0x%x" $start2)
end2_hex=$(printf "0x%x" $end2)
actual=$(get_object_list $TESTPOOL/$TESTFS $start1_hex:$end1_hex \
    $start2_hex $end2_hex)
log_must test "\n$actual\n" == "\n$expected\n"

# Specifying individual object IDs works
objects="$start1 $end1 $start2 $end2"
expected="$objects"
actual=$(get_object_list $TESTPOOL/$TESTFS $objects | awk '{printf("%s ", $1)}' | tr '\n' ' ')
log_must test "${actual% }" == "$expected"

# Get all objects in the meta-objset to test m (spacemap) and z (zap) flags
all_mos_objects=$(get_object_list $TESTPOOL 0:-1)

# Range 0:-1:m must output all space map objects
expected=$(grep "SPA space map" <<< $all_mos_objects)
actual=$(get_object_list $TESTPOOL 0:-1:m)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:z must output all zap objects
expected=$(grep "zap" <<< $all_mos_objects)
actual=$(get_object_list $TESTPOOL 0:-1:z)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:A-m-z must output all non-space maps and non-zaps
expected=$(grep -v -e "zap" -e "SPA space map" <<< $all_mos_objects)
actual=$(get_object_list $TESTPOOL 0:-1:A-m-z)
log_must test "\n$actual\n" == "\n$expected\n"

# Range 0:-1:mz must output all space maps and zaps
expected=$(grep -e "SPA space map" -e "zap" <<< $all_mos_objects)
actual=$(get_object_list $TESTPOOL 0:-1:mz)
log_must test "\n$actual\n" == "\n$expected\n"

log_pass "zdb -dd object range arguments work correctly"
