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
#	Checking if allocation_classes feature flag status is active after
#	creating a pool with a special device.
#
claim="Checking active allocation classes feature flag status successful."

verify_runnable "global"

log_assert $claim
log_onexit cleanup

log_must disk_setup

typeset ac_value
typeset stype=""
typeset sdisks=""

for type in "" "mirror" "raidz"
do
	if [ "$type" = "mirror" ]; then
		stype="mirror"
		sdisks="${CLASS_DISK0} ${CLASS_DISK1} ${CLASS_DISK2}"
	elif [ "$type" = "raidz" ]; then
		stype="mirror"
		sdisks="${CLASS_DISK0} ${CLASS_DISK1}"
	else
		stype=""
		sdisks="${CLASS_DISK0}"
	fi

	log_must zpool create $TESTPOOL $type $ZPOOL_DISKS \
	    special $stype $sdisks

	ac_value="$(zpool get -H -o property,value all | \
	    grep allocation_classes | nawk '{print $2}')"
	if [ "$ac_value" = "active" ]; then
		log_note "feature@allocation_classes is active"
	else
		log_fail "feature@allocation_classes not active, \
		    status = $ac_value"
	fi

	log_must zpool destroy -f $TESTPOOL
done

log_pass $claim
