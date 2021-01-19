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
# Copyright (c) 2017, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS should perform unencrypted sends of encrypted datasets, unless the '-p'
# or '-R' options are specified.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 6. Create a child encryption root
# 2. Snapshot the dataset
# 3. Attempt a send
# 4. Attempt a send with properties
# 5. Attempt a replication send
# 7. Unmount the parent and unload its key
# 8. Attempt a send of the parent dataset
# 9. Attempt a send of the child encryption root
#

verify_runnable "both"

function cleanup
{
    datasetexists $TESTPOOL/$TESTFS1 && \
        log_must zfs destroy -r $TESTPOOL/$TESTFS1
}

log_onexit cleanup

log_assert "ZFS should perform unencrypted sends of encrypted datasets, " \
	"unless the '-p' or '-R' options are specified"

typeset passphrase="password"
typeset passphrase1="password1"
typeset snap="$TESTPOOL/$TESTFS1@snap"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must eval "echo $passphrase1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1/child"

log_must zfs snapshot -r $snap

log_must eval "zfs send $snap >$TEST_BASE_DIR/devnull"
log_mustnot eval "zfs send -p $snap >$TEST_BASE_DIR/devnull"
log_mustnot eval "zfs send -R $snap >$TEST_BASE_DIR/devnull"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_mustnot eval "zfs send $snap >$TEST_BASE_DIR/devnull"
log_must eval "zfs send $TESTPOOL/$TESTFS1/child@snap >$TEST_BASE_DIR/devnull"

log_pass "ZFS performs unencrypted sends of encrypted datasets, unless the" \
	"'-p' or '-R' options are specified"
