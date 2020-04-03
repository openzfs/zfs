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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Attempt to take a checkpoint while a removal is
#	in progress. The attempt should fail.
#
# STRATEGY:
#	1. Create pool with one disk
#	2. Create a big file in the pool, so when the disk
#	   is later removed, it will give us enough of a
#	   time window to attempt the checkpoint while the
#	   removal takes place
#	3. Add a second disk where all the data will be moved
#	   to when the first disk will be removed.
#	4. Start removal of first disk
#	5. Attempt to checkpoint (attempt should fail)
#

verify_runnable "global"

function callback
{
	log_mustnot zpool checkpoint $TESTPOOL
	return 0
}

#
# Create pool
#
setup_test_pool
log_onexit cleanup_test_pool
populate_test_pool

#
# Create big empty file and do some writes at random
# offsets to ensure that it takes up space. Note that
# the implicitly created filesystem ($FS0) does not
# have compression enabled.
#
log_must mkfile $BIGFILESIZE $FS0FILE
log_must randwritecomp $FS0FILE 1000

#
# Add second disk
#
log_must zpool add $TESTPOOL $EXTRATESTDISK

#
# Remove disk and attempt to take checkpoint
#
log_must attempt_during_removal $TESTPOOL $TESTDISK callback
log_must zpool status $TESTPOOL

log_pass "Attempting to checkpoint during removal fails as expected."
