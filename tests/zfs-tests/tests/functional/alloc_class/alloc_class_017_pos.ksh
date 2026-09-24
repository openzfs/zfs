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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	A special or dedup device which can tolerate more device failures
#	than the normal vdevs of a redundant pool is accepted without -f.
#	One which tolerates fewer failures, or which is added to a
#	non-redundant pool, is still rejected.
#
# STRATEGY:
#	1. Create redundant pools with more redundant special/dedup mirrors.
#	2. Add a more redundant special mirror to an existing raidz pool.
#	3. Verify less redundant special mirrors are still rejected.
#

verify_runnable "global"

claim="A more redundant special or dedup device is accepted."

log_assert $claim
log_onexit cleanup

log_must disk_setup

# raidz1 tolerates one failure, a 3-way mirror tolerates two.
for class in special dedup; do
	log_must zpool create $TESTPOOL raidz $ZPOOL_DISKS \
	    $class mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
	log_must zpool destroy -f $TESTPOOL
done

log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_must zpool destroy -f $TESTPOOL

log_must zpool create $TESTPOOL draid $ZPOOL_DISKS \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_must zpool destroy -f $TESTPOOL

log_must zpool create $TESTPOOL raidz $ZPOOL_DISKS
log_must zpool add $TESTPOOL special mirror \
    $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_must zpool iostat -H $TESTPOOL $CLASS_DISK2
log_must zpool destroy -f $TESTPOOL

# raidz2 tolerates two failures, a 2-way mirror only one.
log_mustnot zpool create $TESTPOOL raidz2 $ZPOOL_DISKS \
    special mirror $CLASS_DISK0 $CLASS_DISK1
log_mustnot poolexists $TESTPOOL

log_must zpool create $TESTPOOL raidz2 $ZPOOL_DISKS
log_mustnot zpool add $TESTPOOL special mirror $CLASS_DISK0 $CLASS_DISK1
log_must zpool destroy -f $TESTPOOL

# A non-redundant pool with a redundant special device is still rejected.
log_mustnot zpool create $TESTPOOL $ZPOOL_DISKS \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_mustnot poolexists $TESTPOOL

log_pass $claim
