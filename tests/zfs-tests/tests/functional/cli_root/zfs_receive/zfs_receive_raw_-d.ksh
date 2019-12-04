#!/bin/ksh -p
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
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zfs receive -d should create the expected encryption hierarchy.
#
# STRATEGY:
# 1. Create an encrypted dataset and a inheriting child
# 2. Snapshot the child dataset
# 2. Create a recursive raw send file from the snapshot
# 3. Destroy the original child filesystem
# 4. Receive the snapshot as a child of the second dataset with '-d'
# 5. Verify the new child can be mounted
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
	rm -f $sendfile
}

log_onexit cleanup

log_assert "zfs receive -d should create the expected encryption hierarchy"

typeset passphrase="password1"

sendfile=$TEST_BASE_DIR/sendfile.$$

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"
log_must zfs create $TESTPOOL/$TESTFS1/child
log_must zfs snapshot $TESTPOOL/$TESTFS1/child@snap
log_must eval "zfs send -Rw $TESTPOOL/$TESTFS1/child@snap > $sendfile"
log_must zfs destroy -r $TESTPOOL/$TESTFS1/child
log_must zfs receive -Fd $TESTPOOL < $sendfile
log_must eval "echo $passphrase | zfs mount -l $TESTPOOL/$TESTFS1/child"

log_pass "zfs receive -d creates the expected encryption hierarchy"
