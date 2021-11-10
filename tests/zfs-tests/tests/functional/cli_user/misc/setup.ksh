#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

# This setup script is moderately complex, as it creates scenarios for all
# of the tests included in this directory. Usually we'd want each test case
# to setup/teardown its own configuration, but this would be time consuming
# given the nature of these tests. However, as a side-effect, one test
# leaving the system in an unknown state could impact other test cases.


DISK=${DISKS%% *}
VOLSIZE=150m
TESTVOL=testvol

# Create a default setup that includes a volume
default_setup_noexit "$DISK" "" "volume"

#
# The rest of this setup script creates a ZFS filesystem configuration
# that is used to test the rest of the zfs subcommands in this directory.
#

# create a snapshot and a clone to test clone promote
log_must zfs snapshot $TESTPOOL/$TESTFS@snap
log_must zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS/clone
# create a file in the filesystem that isn't in the above snapshot
touch $TESTDIR/file.txt


# create a non-default property and a child we can use to test inherit
log_must zfs create $TESTPOOL/$TESTFS/$TESTFS2
log_must zfs set snapdir=hidden $TESTPOOL/$TESTFS


# create an unmounted filesystem to test unmount
log_must zfs create $TESTPOOL/$TESTFS/$TESTFS2.unmounted
log_must zfs unmount $TESTPOOL/$TESTFS/$TESTFS2.unmounted


# send our snapshot to a known file in /tmp
zfs send $TESTPOOL/$TESTFS@snap > $TEST_BASE_DIR/zfstest_datastream.dat
if [ ! -s $TEST_BASE_DIR/zfstest_datastream.dat ]
then
	log_fail "ZFS send datafile was not created!"
fi
log_must chmod 644 $TEST_BASE_DIR/zfstest_datastream.dat


# create a filesystem that has particular properties to test set/get
log_must zfs create -o version=1 $TESTPOOL/$TESTFS/prop
set -A props $PROP_NAMES
set -A prop_vals $PROP_VALS
typeset -i i=0

while [[ $i -lt ${#props[*]} ]]
do
	prop_name=${props[$i]}
	prop_val=${prop_vals[$i]}
	log_must zfs set $prop_name=$prop_val $TESTPOOL/$TESTFS/prop
	i=$(( $i + 1 ))
done

# create a filesystem we don't mind renaming
log_must zfs create $TESTPOOL/$TESTFS/renameme


if is_global_zone && ! is_linux
then
	# create a filesystem we can share
	log_must zfs create $TESTPOOL/$TESTFS/unshared
	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS/unshared

	# create a filesystem that we can unshare
	log_must zfs create $TESTPOOL/$TESTFS/shared
	log_must zfs set sharenfs=on $TESTPOOL/$TESTFS/shared
fi


log_must zfs create -o version=1 $TESTPOOL/$TESTFS/version1
log_must zfs create -o version=1 $TESTPOOL/$TESTFS/allowed
log_must zfs allow everyone create $TESTPOOL/$TESTFS/allowed

if is_global_zone
then

	# Now create several virtual disks to test zpool with

	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk1.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk2.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk3.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk-additional.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk-export.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk-offline.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk-spare1.dat
	mkfile $MINVDEVSIZE $TEST_BASE_DIR/disk-spare2.dat

	# and create a pool we can perform attach remove replace,
	# etc. operations with
	log_must zpool create $TESTPOOL.virt mirror $TEST_BASE_DIR/disk1.dat \
	$TEST_BASE_DIR/disk2.dat $TEST_BASE_DIR/disk3.dat \
	$TEST_BASE_DIR/disk-offline.dat spare $TEST_BASE_DIR/disk-spare1.dat


	# Offline one of the disks to test online
	log_must zpool offline $TESTPOOL.virt $TEST_BASE_DIR/disk-offline.dat


	# create an exported pool to test import
	log_must zpool create $TESTPOOL.exported $TEST_BASE_DIR/disk-export.dat
	log_must zpool export $TESTPOOL.exported

	set -A props $POOL_PROPS
	set -A prop_vals $POOL_VALS
	typeset -i i=0

	while [[ $i -lt ${#props[*]} ]]
	do
		prop_name=${props[$i]}
		prop_val=${prop_vals[$i]}
		log_must zpool set $prop_name=$prop_val $TESTPOOL
		i=$(( $i + 1 ))
	done

	# copy a v1 pool from cli_root
	cp $STF_SUITE/tests/functional/cli_root/zpool_upgrade/blockfiles/zfs-pool-v1.dat.bz2 \
	    $TEST_BASE_DIR/
	log_must bunzip2 $TEST_BASE_DIR/zfs-pool-v1.dat.bz2
	log_must zpool import -d $TEST_BASE_DIR/ v1-pool
fi
log_pass
