#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The parity property reports the minimum redundancy of stored epochs.
#
# STRATEGY:
#	Check unchanged pools and tables that raise and lower parity.  The
#	property must retain the original guarantee when raising parity and
#	report the lower guarantee when lowering it, including after reimport.
#	Old and new filesystem data must remain intact through scrub.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_properties.$$
typeset -r POOL=${TESTPOOL}_pe_properties
typeset -a disks

typeset dir mnt

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -rf $BASEDIR
}

function check_parity
{
	typeset value
	value=$(zpool get -Hp -o value parity $POOL raidz$1-0) ||
	    log_fail "could not read the vdev parity property"
	[[ $value == $2 ]] || log_fail "parity property is $value, expected $2"
}

function property_case
{
	typeset name=$1 base=$2 after=$3 minimum=$4
	dir=$BASEDIR/$name
	mnt=$dir/mnt
	log_must mkdir -p $mnt
	for i in 0 1 2 3 4; do
		disks[i]=$dir/v$i
		log_must truncate -s 256M ${disks[i]}
	done
	log_must zpool create -f -o cachefile=none -O mountpoint=$mnt \
	    -O compression=off $POOL raidz$base ${disks[@]}
	log_must cp $BASEDIR/expected $mnt/before
	sync_pool $POOL
	check_parity $base $base
	log_must zpool export $POOL
	if [[ $after != none ]]; then
		log_must zhack -d $dir raidz_epochs $POOL 0 \
		    0:5:$base next:5:$after
	fi
	log_must zpool import -d $dir $POOL
	check_parity $base $minimum
	for i in 0 1 2 3; do
		log_must cp $BASEDIR/expected $mnt/after$i
		sync_pool $POOL
	done
	log_must zpool scrub -w $POOL
	log_must check_pool_status $POOL "errors" "No known data errors"
	log_must zpool export $POOL
	log_must zpool import -d $dir $POOL
	check_parity $base $minimum
	log_must cmp $BASEDIR/expected $mnt/before
	for i in 0 1 2 3; do
		log_must cmp $BASEDIR/expected $mnt/after$i
	done
	log_must zpool destroy $POOL
}

log_onexit cleanup
log_assert "the vdev parity property describes its guaranteed redundancy"
log_must mkdir -p $BASEDIR
log_must dd if=/dev/urandom of=$BASEDIR/expected bs=128k count=64
property_case plain1 1 none 1
property_case plain2 2 none 2
property_case raise 1 2 1
property_case lower 2 1 1
log_pass "the parity property retains the weakest epoch guarantee"
