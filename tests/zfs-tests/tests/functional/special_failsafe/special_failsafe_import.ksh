#!/bin/ksh -p

# Copyright (C) 2024 Lawrence Livermore National Security, LLC.
# Refer to the OpenZFS git commit log for authoritative copyright attribution.
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License Version 1.0 (CDDL-1.0).
# You can obtain a copy of the license from the top-level file
# "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
# You may not use this file except in compliance with the license.
#
# Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049)

. $STF_SUITE/tests/functional/special_failsafe/special_failsafe.kshlib

#
# DESCRIPTION:
# 	Verify we can import a special_failsafe pool even if all its alloc class
# 	devices are missing.
#
verify_runnable "global"

claim="Verify imports work on special_failsafe pools when vdevs missing"

log_assert $claim
log_onexit cleanup

TWO_ZPOOL_DISKS="$ZPOOL_DISK0 $ZPOOL_DISK1"
REPLACE_DISK="$ZPOOL_DISK2"

# Try a bunch of different pool configurations
configs="$TWO_ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $TWO_ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$TWO_ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$TWO_ZPOOL_DISKS special $CLASS_DISK0
$TWO_ZPOOL_DISKS dedup $CLASS_DISK0"

function do_test {
	typeset config="$1"
	typeset action="$2"
	typeset onoff="$3"

	totalwritten=0
	log_must disk_setup
	log_must zpool create -o special_failsafe=$onoff $TESTPOOL $config

	alloc_class_disks="$(get_list_of_alloc_class_disks)"

	special_failsafe_make_datasets
	write_some_files
	verify_all_directories

	log_must zpool export $TESTPOOL

	# Backup alloc class disk before removing them
	backup_alloc_class_disks $alloc_class_disks
	if [ "$action"  == "remove" ] ; then
		rm -f $alloc_class_disks
	else
		zero_alloc_class_disks $alloc_class_disks
	fi

	# import should succeed or fail depending on how we're backed up
	if [ "$onoff" == "on" ] ; then
		log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL
	else
		log_mustnot zpool import -l -d "$IMPORTDIR" $TESTPOOL

		# With the disks restored, we should be able to import
		restore_alloc_class_disks $alloc_class_disks
		log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL
	fi
	write_some_files

	# Do a scrub and verify everything is correct
	verify_pool $TESTPOOL

	verify_all_directories

	zpool destroy $TESTPOOL

	cleanup
}

echo "$configs" | while read config ; do
	for action in "remove" "zero" ; do
		for onoff in "off" "on" ; do
			do_test "$config" "$action" "$onoff"
		done
	done
done

log_pass $claim
