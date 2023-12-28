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
# 	Verify we can offline special_failsafe alloc class disks.
# 	Verify we cannot offline non-special_failsafe alloc class disks.
#
verify_runnable "global"

claim="Verify correct behavior when we force fault an alloc class disk"

log_assert $claim
log_onexit cleanup

# Try a bunch of different pool configurations
configs="mirror $ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$ZPOOL_DISKS special $CLASS_DISK0
$ZPOOL_DISKS dedup $CLASS_DISK0"

function do_test {
	prop="$1"
	config="$2"
	log_must disk_setup
	log_must zpool create -f $prop $TESTPOOL $config
	check_pool_alloc_class_props

	special_failsafe_make_datasets
	totalwritten=0
	write_some_files

	alloc_class_disks=$(get_list_of_alloc_class_disks)
	alloc_class_disks_arr=($alloc_class_disks)

	if [ "$prop" == "-o special_failsafe=on" ] ; then
		log_must [ "$(get_pool_prop feature@special_failsafe $TESTPOOL)" == "active" ]
	else
		log_must [ "$(get_pool_prop feature@special_failsafe $TESTPOOL)" == "enabled" ]
	fi

	for ((i = 0; i < ${#alloc_class_disks_arr[@]}; i++)); do
		disk="${alloc_class_disks_arr[$i]}"
		if [ "$prop" == "-o special_failsafe=on" ] ; then
			# Everything is backed-up.  We should be able to
			# offline all the disks.
			log_must zpool offline $TESTPOOL $disk
			log_must check_state $TESTPOOL "$disk" "OFFLINE"
			log_must check_state $TESTPOOL "" "DEGRADED"
		else
			PARENT=$(get_vdev_prop parent $TESTPOOL $disk)
			if [ "$PARENT" == "$TESTPOOL" ] ; then
				# Leaf is TLD, offline should fail
				log_mustnot zpool offline $TESTPOOL $disk
				log_must check_state $TESTPOOL "$disk" "ONLINE"
				log_must check_state $TESTPOOL "" "ONLINE"
			else
				# We're part of a mirror.  We know all
				# mirrors in our test pool are two disk
				# so we should be able to offline the
				# first disk, but not the second.
				if [ "$i" == "0" ] ; then
					# First alloc class disk - pretend
					# "previous" disk was online to
					# make things easy.
					prev_online=1
				else
					if check_state $TESTPOOL "${alloc_class_disks_arr[$i - 1]}" "ONLINE" ; then
						prev_online=1
					else
						prev_online=0
					fi
				fi

				if [ "$prev_online" == "1" ] ; then
					# First disk in mirror, can offline
					log_must zpool offline $TESTPOOL $disk
					log_must check_state $TESTPOOL "$disk" "OFFLINE"
					log_must check_state $TESTPOOL "" "DEGRADED"
				else
					# Second disk in mirror, can't offline
					# but we should still be in a pool
					# degraded state from the first disk
					# going offline.
					log_mustnot zpool offline $TESTPOOL $disk
					log_must check_state $TESTPOOL "$disk" "ONLINE"
					log_must check_state $TESTPOOL "" "DEGRADED"
				fi
			fi
		fi
	done

	write_some_files
	verify_all_directories

	# We've checked all the files.  Do some more verifications.
	verify_pool $TESTPOOL
	verify_filesys $TESTPOOL $TESTPOOL $IMPORTDIR

	zpool clear $TESTPOOL
	zpool destroy $TESTPOOL
	cleanup
}

for prop in "-o special_failsafe=on" "" ; do
	echo "$configs" | while read config ; do
		do_test "$prop" "$config"
	done
done

log_pass $claim
