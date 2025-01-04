#! /bin/ksh -p
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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

zdbout=$(mktemp)

if is_linux; then
	log_unsupported "ZDB fails during concurrent pool activity."
fi

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $zdbout
}

default_setup_noexit "$DISKS"
log_onexit cleanup
FIRSTDISK=${DISKS%% *}

DISKPATH=/dev
case $FIRSTDISK in
	/*)
		DISKPATH=$(dirname $FIRSTDISK)
		;;
esac

function callback
{
	typeset count=$1
	typeset zdbstat

	log_must zpool set cachefile=none $TESTPOOL
	zdb -e -p $DISKPATH -cudi $TESTPOOL >$zdbout 2>&1
	zdbstat=$?
	log_must zpool set cachefile= $TESTPOOL
	if [[ $zdbstat != 0 ]]; then
		log_note "Output: zdb -e -p $DISKPATH -cudi $TESTPOOL"
		cat $zdbout
		log_note "zdb detected errors with exist status $zdbstat."
	fi
	log_note "zdb -e -p $DISKPATH -cudi $TESTPOOL >zdbout 2>&1"
	return 0
}

test_removal_with_operation callback

log_must zpool set cachefile=none $TESTPOOL
zdb -e -p $DISKPATH -cudi $TESTPOOL >$zdbout 2>&1
zdbstat=$?
log_must zpool set cachefile= $TESTPOOL
if [[ $zdbstat != 0 ]]; then
	log_note "Output following removal: zdb -cudi $TESTPOOL"
	cat $zdbout
	log_fail "zdb detected errors with exit status " \
	    "$zdbstat following removal."
fi

log_pass "Can use zdb during removal"
