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
#	Checking allocation_classes feature flag value after pool is created
#	(should be enabled) and also after a special device added to existing
#	pool (should be active).
#

verify_runnable "global"

log_assert "Values of allocation_classes feature flag correct."
log_onexit cleanup

log_must disk_setup

typeset ac_value

for type in "" "mirror" "raidz"
do
	if [ "$type" = "mirror" ]; then
		log_must zpool create $TESTPOOL $type $ZPOOL_DISK0 $ZPOOL_DISK1
	else
		log_must zpool create $TESTPOOL $type $ZPOOL_DISKS
	fi
	ac_value="$(zpool get -H -o property,value all | \
	    grep allocation_classes  | awk '{print $2}')"
	if [ "$ac_value" = "enabled" ]; then
		log_note "feature@allocation_classes is enabled"
	else
		log_fail "feature@allocation_classes not enabled, \
		    status = $ac_value"
	fi

	if [ "$type" = "" ]; then
		log_must zpool add $TESTPOOL special $CLASS_DISK0
	else
		log_must zpool add $TESTPOOL special mirror \
		    $CLASS_DISK0 $CLASS_DISK1
	fi
	ac_value="$(zpool get -H -o property,value all | \
	    grep allocation_classes | awk '{print $2}')"
	if [ "$ac_value" = "active" ]; then
		log_note "feature@allocation_classes is active"
	else
		log_fail "feature@allocation_classes not active, \
		    status = $ac_value"
	fi

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Values of allocation_classes feature flag correct."
