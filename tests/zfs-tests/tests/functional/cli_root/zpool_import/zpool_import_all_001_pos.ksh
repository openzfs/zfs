#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
# Verify that 'zpool import -a' succeeds as root.
#
# STRATEGY:
# 1. Create a group of pools with specified vdev.
# 2. Create zfs filesystems within the given pools.
# 3. Export the pools.
# 4. Verify that import command succeed.
#

verify_runnable "global"

set -A options "" "-R $ALTER_ROOT"

typeset -A testpools
typeset -i number=0
typeset -i i=0
typeset checksum1
typeset poolname

function setup_single_disk #disk #pool #fs #mtpt
{
	typeset disk=$1
	typeset pool=$2
	typeset fs=${3##/}
	typeset mtpt=$4

	setup_filesystem "$disk" "$pool" "$fs" "$mtpt"
	log_must cp $MYTESTFILE $mtpt/$TESTFILE0
	log_must zpool export $pool

	[[ -d $mtpt ]] && \
		rm -rf $mtpt
}

function cleanup_all
{
	typeset -i id=1

	#
	# Try import individually if 'import -a' failed.
	#
	for pool in $(zpool import | awk '/pool:/ {print $2}'); do
		zpool import -f $pool
	done

	for pool in $(zpool import -d $DEVICE_DIR | awk '/pool:/ {print $2}'); do
		log_must zpool import -d $DEVICE_DIR -f $pool
	done

	while (( id < number )); do
		if poolexists ${TESTPOOL}-$id ; then
			cleanup_filesystem "${TESTPOOL}-$id" $TESTFS
			destroy_pool ${TESTPOOL}-$id
		fi
		(( id = id + 1 ))
        done

	[[ -d $ALTER_ROOT ]] && \
		rm -rf $ALTER_ROOT
}

function checksum_all #alter_root
{
	typeset alter_root=$1
	typeset -i id=1
	typeset file
	typeset checksum2

	while (( id < number )); do
		file=${alter_root}/$TESTDIR.$id/$TESTFILE0
		[[ ! -e $file ]] && \
			log_fail "$file missing after import."

		read -r checksum2 _ < <(cksum $file)
		log_must [ "$checksum1" = "$checksum2" ]

		(( id = id + 1 ))
	done

	return 0
}


log_assert "Verify that 'zpool import -a' succeeds as root."
log_onexit cleanup_all

read -r checksum1 _ < <(cksum $MYTESTFILE)
number=1

#
# setup exported pools on raw files
#
for disk in $DEVICE_FILES
do
	poolname="${TESTPOOL}-$number"
	setup_single_disk "$disk" \
		"$poolname" \
		"$TESTFS" \
		"$TESTDIR.$number"
	testpools[$poolname]=$poolname
	(( number = number + 1 ))
done

while (( i < ${#options[*]} )); do

	log_must zpool import -d $DEVICE_DIR ${options[i]} -a -f

	# export unintentionally imported pools
	for poolname in $(get_all_pools); do
		if [[ -z ${testpools[$poolname]} ]]; then
			log_must_busy zpool export $poolname
		fi
	done

	if [[ -n ${options[i]} ]]; then
		checksum_all $ALTER_ROOT
	else
		checksum_all
	fi

	for poolname in ${testpools[@]}; do
		if poolexists $poolname ; then
			log_must_busy zpool export $poolname
		fi
	done

	(( i = i + 1 ))
done

log_pass "'zpool import -a' succeeds as root."
