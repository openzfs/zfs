#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

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
# Copyright (c) 2020 by Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -d pool/<objset id> will display the dataset
#
# Strategy:
# 1. Create a pool
# 2. Write some data to a file
# 3. Get the inode number (object number) of the file
# 4. Run zdb -d to get the objset ID of the dataset
# 5. Run zdb -dddddd pool/objsetID objectID (decimal)
# 6. Confirm names
# 7. Run zdb -dddddd pool/objsetID objectID (hex) 
# 8. Confirm names
# 9. Repeat with zdb -NNNNNN pool/objsetID objectID
# 10. Obtain dataset name from testpool.objset-0x<objsetID>.dataset_name kstat
# 11. Run zdb -dddddd pool/objsetID (hex)
# 12. Match name from zdb against kstat
# 13. Create dataset with hex numeric name
# 14. Create dataset with decimal numeric name
# 15. zdb -d for numeric datasets succeeds
# 16. zdb -N for numeric datasets fails
# 17. zdb -dN for numeric datasets fails
#

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify zdb -d <pool>/<objset ID> generates the correct names."
log_onexit cleanup
init_data=$TESTDIR/file1
write_count=8
blksize=131072
verify_runnable "global"
verify_disk_count "$DISKS" 2
hex_ds=$TESTPOOL/0x400000
num_ds=$TESTPOOL/100000

default_mirror_setup_noexit $DISKS
file_write -o create -w -f $init_data -b $blksize -c $write_count

# get object number of file
listing=$(ls -i $init_data)
set -A array $listing
obj=${array[0]}
log_note "file $init_data has object number $obj"
sync_pool $TESTPOOL

IFS=", " read -r _ _ _ _ objset_id _ < <(zdb -d $TESTPOOL/$TESTFS)
objset_hex=$(printf "0x%x" $objset_id)
log_note "objset $TESTPOOL/$TESTFS has objset ID $objset_id ($objset_hex)"

for id in "$objset_id" "$objset_hex"
do
	log_note "zdb -dddddd $TESTPOOL/$id $obj"
	output=$(zdb -dddddd $TESTPOOL/$id $obj)
	echo $output | grep -q "$TESTPOOL/$TESTFS" ||
		log_fail "zdb -dddddd $TESTPOOL/$id $obj failed ($TESTPOOL/$TESTFS not in zdb output)"
	echo $output | grep -q "file1" ||
		log_fail "zdb -dddddd $TESTPOOL/$id $obj failed (file1 not in zdb output)"

	obj=$(printf "0x%X" $obj)
	log_note "zdb -NNNNNN $TESTPOOL/$id $obj"
	output=$(zdb -NNNNNN $TESTPOOL/$id $obj)
	echo $output | grep -q "$TESTPOOL/$TESTFS" ||
		log_fail "zdb -NNNNNN $TESTPOOL/$id $obj failed ($TESTPOOL/$TESTFS not in zdb output)"
	echo $output | grep -q "file1" ||
		log_fail "zdb -NNNNNN $TESTPOOL/$id $obj failed (file1 not in zdb output)"
done

name_from_proc=$(kstat_dataset -N $TESTPOOL/$objset_id dataset_name)
log_note "checking zdb output for $name_from_proc"
log_must eval "zdb -dddddd $TESTPOOL/$objset_hex | grep -q \"$name_from_proc\""

log_must zfs create $hex_ds
log_must zfs create $num_ds
log_must eval "zdb -d $hex_ds | grep -q \"$hex_ds\""
log_must eval "zdb -d $num_ds | grep -q \"$num_ds\""

# force numeric interpretation, expect fail
log_mustnot zdb -N $hex_ds
log_mustnot zdb -N $num_ds
log_mustnot zdb -Nd $hex_ds
log_mustnot zdb -Nd $num_ds

log_pass "zdb -d <pool>/<objset ID> generates the correct names."
