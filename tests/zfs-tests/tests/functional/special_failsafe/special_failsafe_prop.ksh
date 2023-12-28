#!/bin/ksh -p

# Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
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
# 	Verify that special_failsafe prop does not work if
# 	SPA_FEATURE_SPECIAL_FAILSAFE is disabled.  Also, test upgrades.

verify_runnable "global"

claim="special_failsafe prop shouldn't work without SPA_FEATURE_SPECIAL_FAILSAFE"

log_assert $claim
log_onexit cleanup

# Try a bunch of different pool configurations
configs="$ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1  dedup $CLASS_DISK2 $CLASS_DISK3
raidz $ZPOOL_DISKS special mirror $CLASS_DISK0 $CLASS_DISK1 dedup mirror $CLASS_DISK2 $CLASS_DISK3
$ZPOOL_DISKS special $CLASS_DISK0 dedup $CLASS_DISK1
$ZPOOL_DISKS special $CLASS_DISK0
$ZPOOL_DISKS dedup $CLASS_DISK0"

# Make the pool disks smaller to make them quicker to back up.  We don't use
# much data on them.
export ZPOOL_DEVSIZE=200M
export CLASS_DEVSIZE=200M

log_must disk_setup

echo "$configs" | while read config ; do
	# We should not be able to set special_failsafe=on if the feature
	# flag is disabled.
	log_mustnot zpool create -o feature@special_failsafe=disabled -o special_failsafe=on $TESTPOOL $config

	# Try a few permutations that should succeed
	log_must zpool create -o special_failsafe=off $TESTPOOL $config
	boilerplate_check "enabled" "off"
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o special_failsafe=on $TESTPOOL $config
	boilerplate_check "active" "on"
	log_must zpool destroy $TESTPOOL

	log_must zpool create -o feature@special_failsafe=enabled -o special_failsafe=on $TESTPOOL $config
	boilerplate_check "active" "on"
	log_must zpool destroy $TESTPOOL
done

# Now let's do a multi-step test where we upgrade an older pool
for cmd in "zpool set feature@special_failsafe=enabled $TESTPOOL" "zpool upgrade $TESTPOOL" ; do

	# Make a pool with no special devices
	log_must zpool create -o feature@special_failsafe=disabled -o special_failsafe=off $TESTPOOL mirror $ZPOOL_DISKS
	totalwritten=0

	boilerplate_check "disabled" "off"
	special_failsafe_make_datasets
	write_some_files

	# Test enabling the feature in two different ways:
	#
	#     zpool set feature@special_failsafe=enabled ...
	#     zpool upgrade ...
	#
	log_must eval "$cmd"
	boilerplate_check "enabled" "off"
	write_some_files

	# Shouldn't be able to add with special_failsafe prop off
	log_mustnot zpool add $TESTPOOL special $CLASS_DISK0

	log_must zpool set special_failsafe=on $TESTPOOL
	boilerplate_check "enabled" "on"
	write_some_files

	log_must zpool add $TESTPOOL special $CLASS_DISK0

	boilerplate_check "active" "on"

	write_some_files

	zpool add $TESTPOOL dedup $CLASS_DISK1

	write_some_files

	log_must zpool export $TESTPOOL
	log_must zpool import -l -d $IMPORTDIR $TESTPOOL

	verify_all_directories

	# You should be able to turn special_failsafe off if it was on
	log_must zpool set special_failsafe=off $TESTPOOL

	boilerplate_check "active" "off"

	# If special_failsafe prop was on and the feature active, and then you
	# turned the prop off, you cannot turn it back on again.
	log_mustnot zpool set special_failsafe=on $TESTPOOL

	log_must zpool destroy $TESTPOOL
done

# Verify the special_failsafe prop persists across imports
log_must zpool create -o special_failsafe=on $TESTPOOL $ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1
log_must zpool export $TESTPOOL
log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL
typeset prop=$(get_pool_prop special_failsafe $TESTPOOL)
log_must [ "$prop" == "on" ]
log_must zpool destroy $TESTPOOL

log_must zpool create $TESTPOOL $ZPOOL_DISKS special $CLASS_DISK0 $CLASS_DISK1
log_must zpool export $TESTPOOL
log_must zpool import -l -d "$IMPORTDIR" $TESTPOOL
typeset prop=$(get_pool_prop special_failsafe $TESTPOOL)
log_must [ "$prop" == "off" ]
log_must zpool destroy $TESTPOOL

cleanup

log_pass $claim
