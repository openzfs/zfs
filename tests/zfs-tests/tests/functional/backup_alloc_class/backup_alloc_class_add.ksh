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

. $STF_SUITE/tests/functional/backup_alloc_class/backup_alloc_class.kshlib

#
# DESCRIPTION:
# 	Verify that 'zpool add' and 'zpool attach' disks have the correct backup
# 	to pool settings.

verify_runnable "global"

claim="zpool add|attach disks have correct backup_to_pool settings"

log_assert $claim
log_onexit cleanup

# Try different pool configurations
configs="mirror $ZPOOL_DISK0 $ZPOOL_DISK1 special mirror $CLASS_DISK0 $CLASS_DISK1
mirror $ZPOOL_DISK0 $ZPOOL_DISK1 dedup mirror $CLASS_DISK0 $CLASS_DISK1"

log_must disk_setup

function do_test {
	typeset config="$1"
	typeset initial=$2
	typeset new=$3

	log_must zpool create -o backup_alloc_class_to_pool=$initial $TESTPOOL $config
	totalwritten=0

	boilerplate_check "active" "$initial" "$initial"
	backup_alloc_class_make_datasets
	write_some_files

	log_must zpool set backup_alloc_class_to_pool=$new $TESTPOOL

	# We've just set backup_alloc_class_to_pool (possibly) a new value.  Check
	# that our new value still gives us the right props.
	if [ $new == "off" ] || [ $initial == "off" ] ; then
		initial_expected="off"
	else
		initial_expected="on"
	fi

	# Attach to our special/dedup mirror.  New device should be fully
	# backed up, but the old devices should remain not baked up.
       alloc_class_disks="$(get_list_of_alloc_class_disks)"
	log_must zpool attach $TESTPOOL $CLASS_DISK0 $CLASS_DISK2
	check_backup_to_pool_is "$initial_expected" "$alloc_class_disks"
	check_backup_to_pool_is "$new" "$CLASS_DISK2"
	write_some_files

	# Now add a new special/dedup disk.  It should be backed up.
	log_must zpool add $TESTPOOL special $CLASS_DISK4

	check_backup_to_pool_is "$initial_expected" "$alloc_class_disks"
	check_backup_to_pool_is "$new" "$CLASS_DISK2 $CLASS_DISK4"

	write_some_files
	verify_all_directories

	log_must zpool export $TESTPOOL
	log_must zpool import -l -d $IMPORTDIR $TESTPOOL

	verify_all_directories

	log_must zpool destroy $TESTPOOL
}

# Create a pool that is initially not backed up.  Then, enable backups
# and add/attach a disk.  The new disks should be backed up, but the
# old disks should not be backed up.
echo "$configs" | while read config ; do
	for initial in "on" "off" ; do
		for new in "on" "off" ; do
			do_test "$config" $initial $new
		done
	done
done

cleanup

log_pass $claim
