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
#	Adding an additional special device to a pool with special succeeds.
#
claim="Adding an additional special device succeeds."

verify_runnable "global"

log_assert $claim
log_onexit cleanup

log_must disk_setup

typeset special_type=""
typeset create_disks=""
typeset added_disks=""

for type in "" "raidz"
do
	if [ "$type" = "raidz" ]; then
		special_type="mirror"
		create_disks="${CLASS_DISK0} ${CLASS_DISK1}"
		added_disks="${CLASS_DISK2} ${CLASS_DISK3}"
	else
		special_type=""
		create_disks="${CLASS_DISK0}"
		added_disks="${CLASS_DISK1}"
	fi
	log_must zpool create $TESTPOOL $type $ZPOOL_DISKS \
	    special $special_type $create_disks
	log_must zpool add $TESTPOOL special $special_type $added_disks
	log_must zpool iostat $TESTPOOL $added_disks
	log_must zpool destroy -f $TESTPOOL
done

log_pass $claim
