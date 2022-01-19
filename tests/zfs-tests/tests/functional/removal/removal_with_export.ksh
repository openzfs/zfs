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
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

default_setup_noexit "$DISKS"
log_must zfs set compression=off $TESTPOOL
log_onexit default_cleanup_noexit

function callback
{
	test_removal_with_operation_kill
	log_must zpool export $TESTPOOL

	#
	# We are concurrently starting dd processes that will
	# create files in $TESTDIR.  These could cause the import
	# to fail because it can't mount on the filesystem on a
	# non-empty directory.  Therefore, remove the directory
	# so that the dd process will fail.
	#
	log_must rm -rf $TESTDIR

	log_must zpool import $TESTPOOL
	return 0
}

test_removal_with_operation callback

log_pass "Can export and import pool during removal"
