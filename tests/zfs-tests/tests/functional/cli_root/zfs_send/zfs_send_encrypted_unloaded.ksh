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
# ZFS should not perform unencrypted sends from encrypted datasets
# with unloaded keys.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Snapshot the dataset
# 3. Unload the dataset key
# 4. Verify sending the stream fails
#

verify_runnable "both"

function cleanup
{
    datasetexists $TESTPOOL/$TESTFS1 && \
        log_must zfs destroy -r $TESTPOOL/$TESTFS1
}

log_onexit cleanup

log_assert "ZFS should not perform unencrypted sends from encrypted datasets" \
	"with unloaded keys."

typeset passphrase="password"
typeset snap="$TESTPOOL/$TESTFS1@snap"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"
log_must zfs snapshot $snap
log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1
log_mustnot eval "zfs send $snap > /dev/null"

log_pass "ZFS does not perform unencrypted sends from encrypted datasets" \
	"with unloaded keys."
