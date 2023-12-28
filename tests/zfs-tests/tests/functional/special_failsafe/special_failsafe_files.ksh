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
# 	Test multiple different special_failsafe permutations.  After each step
# 	write a bunch of known files.  Verify all files are present and correct
# 	after all the steps are complete.

verify_runnable "global"

claim="Files on special_failsafe enabled disks do not get corrupted"

log_assert $claim
log_onexit cleanup

# Try different pool configurations
configs="mirror $ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$ZPOOL_DISKS special $CLASS_DISK0
$ZPOOL_DISKS dedup $CLASS_DISK0"

echo "$configs" | while read config ; do
	log_must disk_setup
	log_must zpool create -o special_failsafe=on $TESTPOOL $config
	totalwritten=0
	special_failsafe_make_datasets

	write_some_files
	verify_all_directories

	alloc_class_disks="$(get_list_of_alloc_class_disks)"
	log_must zpool export $TESTPOOL

	backup_alloc_class_disks $alloc_class_disks
	zero_alloc_class_disks $alloc_class_disks

	log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL

	# Our pool is imported but has all its special devices zeroed out.  Try
	# writing some files to it and export the pool
	write_some_files

	log_must zpool export $TESTPOOL
	log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL

	write_some_files

	log_must zpool export $TESTPOOL
	log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL

	write_some_files

	# Make our old disks appear again (which have older data).  Do a zpool
	# clear to make them come back online and resilver.
	restore_alloc_class_disks $alloc_class_disks
	log_must zpool clear $TESTPOOL

	write_some_files

	# At this point the pool should be normal.  The next test is to
	# corrupt the alloc class devices while the pool is running.
	zero_alloc_class_disks $alloc_class_disks

	# Trigger a scrub with our newly-zeroed alloc class disks
	log_must zpool scrub $TESTPOOL

	# The pool should be degraded, but still alive.
	check_state $TESTPOOL "" "DEGRADED"

	write_some_files

	# Replace all the alloc class disks.  This should get the pool
	# back to normal.
	for disk in $alloc_class_disks ; do
		log_must zpool replace $TESTPOOL $disk
	done

	write_some_files

	log_must zpool export $TESTPOOL

	# Backup special disks, then totally remove them.
	backup_alloc_class_disks $alloc_class_disks

	rm -f $alloc_class_disks

	# Try to import with the alloc class disks missing - it should work.
	log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL

	# After all the pain we've put our pool though, it should still have all the
	# correct file data.
	log_must verify_all_directories

	if [[ "$totalwritten" != "840" ]] ; then
		log_fail "Didn't see 840 files, saw $totalwritten"
	fi

	# We've checked all the files.  Do some more verifications.
	verify_pool $TESTPOOL
	verify_filesys $TESTPOOL $TESTPOOL $IMPORTDIR

	# Record a few stats that show metadata re in use
	zpool get dedup $TESTPOOL
	zdb -bb $TESTPOOL 2>&1 | grep -Ei 'normal|special|dedup|ddt'

	log_must zpool destroy $TESTPOOL
	cleanup
done

log_pass $claim
