#!/bin/ksh -p

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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
# 	Can set special_small_blocks property less than or equal to recordsize.
#

verify_runnable "global"

claim="Can set special_small_blocks property less than or equal to recordsize"

log_assert $claim
log_onexit cleanup
log_must disk_setup

for size in 8192 32768 131072 524288 1048576
do
	let smaller=$size/2
	log_must zpool create -O recordsize=$size \
		-O special_small_blocks=$smaller \
		$TESTPOOL raidz $ZPOOL_DISKS special mirror \
		$CLASS_DISK0 $CLASS_DISK1
	log_must zpool destroy -f "$TESTPOOL"

	log_must zpool create -O recordsize=$size \
		-O special_small_blocks=$size \
		$TESTPOOL raidz $ZPOOL_DISKS special mirror \
		$CLASS_DISK0 $CLASS_DISK1
	log_must zpool destroy -f "$TESTPOOL"
done

log_pass $claim
