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
# 	Destroy alloc class disks and then do a scrub on both a
# 	special_failsafe and non-special_failsafe pool.  The special_failsafe
# 	pool should only be DEGRADED, while the non-special_failsafe pool should
# 	be SUSPENDED.

verify_runnable "global"

claim="special_failsafe pools survive a normally fatal scrub with bad disks"

log_assert $claim
log_onexit cleanup

# Try different pool configurations
configs="$ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$ZPOOL_DISKS special $CLASS_DISK0
$ZPOOL_DISKS dedup $CLASS_DISK0"

function do_test {
	typeset config="$1"
	typeset action="$2"
	typeset onoff="$3"
	totalwritten=0

	log_must disk_setup
	log_must zpool create -o feature@special_failsafe=enabled -o special_failsafe=$onoff $TESTPOOL $config

	special_failsafe_make_datasets

	totalwritten=0
	write_some_files

	# When we do a scrub later, we will either want it to suspend or not
	# suspend the pool, depending on our backup settings. Make sure we are
	# able to ride though the suspended pool so we # can continue with our
	# tests.
	log_must zpool set failmode=continue $TESTPOOL

	alloc_class_disks="$(get_list_of_alloc_class_disks)"
	backup_alloc_class_disks $alloc_class_disks
	zero_alloc_class_disks $alloc_class_disks

	# Spawn scrub into the background since the pool may be suspended and
	# it will hang.  We need to continue passed the hung scrub so we
	# can restore the bad disks and do a 'zpool clear' to remove the
	# suspended pool.
	zpool scrub $TESTPOOL &

	wait_scrubbed $TESTPOOL 3
	if [ "$onoff" == "on" ] ; then
		log_must check_state $TESTPOOL "" "DEGRADED"

		verify_pool $TESTPOOL

		write_some_files
		verify_all_directories
	else
		log_must check_state $TESTPOOL "" "SUSPENDED"

		# Pool should be suspended.  Restore the old disks so we can
		# clear the suspension.  'zpool clear' here will delete the
		# pool.
		restore_alloc_class_disks $alloc_class_disks
		log_must zpool clear $TESTPOOL
	fi

	cleanup
}

# Stop zed in case we left it running from an old, aborted, test run.
zed_stop
zed_cleanup

log_must zed_setup
log_must zed_start
log_must zed_events_drain

# Verify scrubs work as expected with different permutations of special_failsafe
echo "$configs" | while read config ; do
	for i in "on" "off" ; do
		do_test "$config" "zero" "$i"
	done
done

log_must zed_stop
log_must zed_cleanup

log_pass $claim
