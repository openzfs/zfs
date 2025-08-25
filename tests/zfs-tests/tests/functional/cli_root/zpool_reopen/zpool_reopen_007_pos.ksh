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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/tests/functional/cli_root/zpool_reopen/zpool_reopen.shlib

#
# DESCRIPTION:
# Test zpool reopen while performing IO to the pool.
# Verify that no IO errors of any kind of reported.
#
# STRATEGY:
# 1. Create a non-redundant pool.
# 2. Repeat:
#   a. Write files to the pool.
#   b. Execute 'zpool reopen'.
# 3. Verify that no errors are reported by 'zpool status'.

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Testing zpool reopen with concurrent user IO"
log_onexit cleanup

set_removed_disk
scsi_host=$(get_scsi_host $REMOVED_DISK)

# 1. Create a non-redundant pool.
log_must zpool create $TESTPOOL $DISK1 $DISK2 $DISK3

for i in $(seq 10); do
	# 3a. Write files in the background to the pool.
	mkfile 64m /$TESTPOOL/data.$i &

	# 3b. Execute 'zpool reopen'.
	log_must zpool reopen $TESTPOOL

	for disk in $DISK1 $DISK2 $DISK3; do
		zpool status -P -v $TESTPOOL | grep $disk | \
		    read -r name state rd wr cksum
		log_must [ $state = "ONLINE" ]
	        log_must [ $rd -eq 0 ]
	        log_must [ $wr -eq 0 ]
	        log_must [ $cksum -eq 0 ]
	done
done

wait

log_pass "Zpool reopen with concurrent user IO successful"
