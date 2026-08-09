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
# Copyright (c) 2026, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Inject an io-prefail error on a child of a raidz device, then write
#	some data and verify that the pool encountered errors.
#

function cleanup
{
	log_pos zpool status $TESTPOOL

	log_must zinject -c all

	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

}

log_onexit cleanup

typeset -r devs=6
typeset -r dev_size_mb=128

typeset -a disks

# Disk files which will be used by pool
for i in {1..$devs}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

function run_test
{
	log_must zpool create -f -o cachefile=none -O recordsize=16k $TESTPOOL raidz1 ${disks[@]}

	log_must zinject -d $TEST_BASE_DIR/dev-1 -e io-prefail -T write -f 25 $TESTPOOL

	log_must file_write -o create -f /$TESTPOOL/file -b 128k -c 1000 -d R
	log_must zpool sync $TESTPOOL
	log_pos check_pool_status $TESTPOOL "errors" "No known data errors" || return 1
	log_pos check_pool_status $TESTPOOL "status" "One or more" || return 1

	log_must zinject -c all
	log_must zpool export -f $TESTPOOL
	log_must rm $TEST_BASE_DIR/dev-2
	log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
	log_must zpool scrub $TESTPOOL
	log_must zpool wait -t scrub $TESTPOOL
	log_pos check_pool_status $TESTPOOL "errors" "No known data" || return 1
	log_pos check_pool_device $TESTPOOL "dev-1" "ONLINE.* 0$" || return 1
}

i=0
while [[ $i -lt 3 ]]; do
	run_test && log_pass "raidz handles partial write failure."
	log_must zinject -c all
	log_must zpool destroy $TESTPOOL
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-2
	i=$((i + 1))
done

log_fail "raidz does not handle partial write failure."
