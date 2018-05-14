#!/usr/bin/ksh

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
# Copyright (c) 2014 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

if ! getholes -n /etc/hosts; then
	log_unsupported "The kernel does not support SEEK_DATA / SEEK_HOLE"
fi

DISK=${DISKS%% *}
default_setup_noexit $DISK

if ! mkholes -h 0:1024 $TESTDIR/setup_test; then
	log_unsupported "The kernel does not support FALLOC_FL_PUNCH_HOLE"
fi
log_must rm $TESTDIR/setup_test
log_pass