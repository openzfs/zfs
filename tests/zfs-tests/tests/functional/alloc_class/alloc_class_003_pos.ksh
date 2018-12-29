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

#
# Copyright (c) 2017, Intel Corporation.
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Adding a special device to a normal pool succeeds.
#
claim="Adding a special device to a normal pool succeeds."

verify_runnable "global"

log_assert $claim
log_onexit cleanup

log_must disk_setup

for type in "" "mirror" "raidz"
do
	log_must zpool create $TESTPOOL $type $ZPOOL_DISKS

	if [ "$type" = "mirror" ]; then
		log_must zpool add $TESTPOOL special mirror \
		    $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK0
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK1
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK2
	elif [ "$type" = "raidz" ]; then
		log_must zpool add $TESTPOOL special mirror \
		    $CLASS_DISK0 $CLASS_DISK1
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK0
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK1
	else
		log_must zpool add $TESTPOOL special $CLASS_DISK0
		log_must zpool iostat -H $TESTPOOL $CLASS_DISK0
	fi

	log_must zpool destroy -f $TESTPOOL
done

log_pass $claim
