#!/bin/ksh -p

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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Removing a special device from a pool succeeds.
#

verify_runnable "global"

#
# Given a dataset and an inode number, return a list of all the vdev numbers
# that the inode has blocks on.
#
# For example, if the inode has blocks on vdevs 0, 1 and 2, this would return
# the string "0 1 2"
#
function vdevs_file_is_on # <dataset> <inode>
{
	typeset dataset="$1"
	typeset inum="$2"
	zdb -dddddd $dataset $inum | awk '
/L0 [0-9]+/{
# find DVAs from string "offset level dva" only for L0 (data) blocks
# if (match($0,"L0 [0-9]+")) {
   dvas[0]=$3
   dvas[1]=$4
   dvas[2]=$5
   for (i = 0; i < 3; ++i) {
      if (match(dvas[i],"([^:]+):.*")) {
         dva = substr(dvas[i], RSTART, RLENGTH);
         # parse DVA from string "vdev:offset:asize"
         if (split(dva,arr,":") != 3) {
            print "Error parsing DVA: <" dva ">";
            exit 1;
         }
        count[arr[1]]++;
      }
   }
#}
}
END {
    # Print out the unique vdev numbers that had data
    firstprint=1;
    for (i in count) {
        if (firstprint==1) {
            printf("%d", i);
            firstprint=0;
        } else {
            printf(" %d", i);
        }
    }
}
'
}

#
# Check that device removal works for special class vdevs
#
# $1: Set to 1 to backup alloc class data to the pool.  Leave blank to disable
#     backup.
function check_removal
{
	typeset backup
	if [ "$1" == "1" ] ; then
		backup=1
		args="-o special_failsafe=on"
	else
		backup=0
		args=""
	fi

	#
	# Create a non-raidz pool so we can remove top-level vdevs
	#
	log_must zpool create $args $TESTPOOL $ZPOOL_DISKS \
	    special $CLASS_DISK0 special $CLASS_DISK1
	log_must display_status "$TESTPOOL"

	#
	# Generate some metadata and small blocks in the special class vdev
	# before removal
	#
	typeset -l i=1
	typeset -l blocks=25

	log_must zfs create -o special_small_blocks=32K -o recordsize=32K \
	    $TESTPOOL/$TESTFS
	for i in 1 2 3 4; do
		log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/testfile.$i \
		    bs=1M count=$blocks
		((blocks = blocks + 25))
	done
	sync_pool $TESTPOOL
	log_must zpool list -v $TESTPOOL

	# Verify the files were written in the special class vdevs
	for i in 1 2 3 4; do
		dataset="$TESTPOOL/$TESTFS"
		inum="$(get_objnum /$TESTPOOL/$TESTFS/testfile.$i)"

		# Get a list of all the vdevs 'testfile.$i' has blocks on.
		# The list will be string like "0 1 2 3" if the blocks are on
		# vdevs 0-3.
		on_vdevs="$(vdevs_file_is_on $dataset $inum)"

		# Get the number of normal (non-special) pool disks
		num_pool_disks=$(echo $ZPOOL_DISKS | wc -w)
		num_pool_disks=${num_pool_disks##* }

		if [ "$backup" == "1" ] ; then
			# Data should be on all vdevs (both pool and special
			# devices).
			lowest_data_disk=0
			highest_data_disk=$(($num_pool_disks + 1))
		else

			# Data should only be on special devices
			lowest_data_disk=$num_pool_disks
			highest_data_disk=$(($lowest_data_disk + 1))
		fi

		# Get the starting disks that we expect the data to be on.
		# We assume two special devices are attached to the pool.
		# Disk numbers start at zero.
		expected_on_vdevs="$(seq -s ' ' $lowest_data_disk $highest_data_disk)"

		# Compare the disks we expect to see the blocks on with
		# the actual disks they're on.
		if [ "$on_vdevs" != "$expected_on_vdevs" ] ; then
			# Data distribution is not what we expected, break out of
			# the loop so we can properly tear down the pool.  We will
			# error out after the loop.
			break;
		fi
	done

	log_must zpool remove $TESTPOOL $CLASS_DISK0
	log_must zpool destroy -f "$TESTPOOL"

	if [ "$on_vdevs" != "$expected_on_vdevs" ] ; then
		log_fail "Expected data on disks $expected_on_vdevs, got $on_vdevs"
	fi
}

claim="Removing a special device from a pool succeeds."

log_assert $claim
log_onexit cleanup

log_must disk_setup
for backup in "1" "" ; do
	typeset CLASS_DEVSIZE=$CLASS_DEVSIZE
	for CLASS_DEVSIZE in $CLASS_DEVSIZE $ZPOOL_DEVSIZE; do
		typeset ZPOOL_DISKS=$ZPOOL_DISKS
		for ZPOOL_DISKS in "$ZPOOL_DISKS" $ZPOOL_DISK0; do
			check_removal $backup
		done
	done
done
log_must disk_cleanup
log_pass $claim
