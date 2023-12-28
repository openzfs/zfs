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
# 	Verify that 'zpool add' and 'zpool attach' disks have the correct
# 	special_failsafe settings.

verify_runnable "global"

claim="zpool add|attach disks have correct special_failsafe settings"

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

	log_must zpool create -o special_failsafe=$initial $TESTPOOL $config
	totalwritten=0

	# Sanity check that feature@special_failsafe aligns with the
	# pool prop
	if [ $initial == "on" ] ; then
		feature_expected="active"
	else
		feature_expected="enabled"
	fi
	boilerplate_check "$feature_expected" "$initial"

	special_failsafe_make_datasets
	write_some_files

	if [ $initial != "off" ] ; then
		log_must zpool set special_failsafe=$new $TESTPOOL
	fi

	write_some_files

	# Now add a new special/dedup disk to the special mirror
	log_must zpool attach $TESTPOOL $CLASS_DISK0 $CLASS_DISK2
	write_some_files

	# Add another special & dedup disk in RAID0 with the existing
	# special mirror
	log_must zpool add $TESTPOOL special $CLASS_DISK3
	log_must zpool add $TESTPOOL dedup $CLASS_DISK4

	write_some_files
	verify_all_directories

	log_must zpool export $TESTPOOL

       alloc_class_disks="$(get_list_of_alloc_class_disks)"
	zero_alloc_class_disks $alloc_class_disks

	log_must zpool import -l -d $IMPORTDIR $TESTPOOL

	verify_all_directories

	log_must zpool destroy $TESTPOOL
}

# Create a pool that is initially not special_failsafe.  Then, enable
# special_failsafe and add/attach a disk.
echo "$configs" | while read config ; do
	for initial in "on" "off" ; do
		for new in "on" "off" ; do
			do_test "$config" $initial $new
		done
	done
done

cleanup

log_pass $claim
