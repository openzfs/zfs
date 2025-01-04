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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Setting the special_small_blocks property greater than recordsize fails.
#

verify_runnable "global"

claim="Setting the special_small_blocks property greater than recordsize fails"

log_assert $claim
log_onexit cleanup
log_must disk_setup

for size in 512 4096 32768 131072 524288 1048576
do
	let bigger=$size*2
	log_mustnot zpool create -O recordsize=$size \
		-O special_small_blocks=$bigger \
		$TESTPOOL raidz $ZPOOL_DISKS special mirror \
		$CLASS_DISK0 $CLASS_DISK1
done

log_pass $claim
