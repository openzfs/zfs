#! /bin/ksh -p
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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

default_setup_noexit "$DISKS"
log_onexit default_cleanup_noexit

for disk in $DISKS; do
	if [[ "$disk" != "$REMOVEDISK" ]]; then
		log_must zpool remove $TESTPOOL $disk
		log_must wait_for_removal $TESTPOOL
		log_mustnot vdevs_in_pool $TESTPOOL $disk
	fi
done

log_must [ "x$(get_disklist $TESTPOOL)" = "x$REMOVEDISK" ]

log_mustnot zpool remove $TESTPOOL $disk

log_pass "Was not able to remove the last device in a pool."
