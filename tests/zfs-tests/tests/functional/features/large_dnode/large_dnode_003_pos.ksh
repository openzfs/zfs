#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
        if datasetexists $LDNPOOL ; then
                log_must zpool destroy -f $LDNPOOL
        fi
}

log_onexit cleanup

log_assert "feature correctly switches between enabled and active"

LDNPOOL=ldnpool
LDNFS=$LDNPOOL/large_dnode
log_must mkfile 64M  $TESTDIR/$LDNPOOL
log_must zpool create $LDNPOOL $TESTDIR/$LDNPOOL


state=$(zpool list -Ho feature@large_dnode $LDNPOOL)
if [[ "$state" != "enabled" ]]; then
        log_fail "large_dnode has state $state (expected enabled)"
fi

log_must zfs create -o dnodesize=1k $LDNFS
log_must touch /$LDNFS/foo
log_must zfs unmount $LDNFS

state=$(zpool list -Ho feature@large_dnode $LDNPOOL)
if [[ "$state" != "active" ]]; then
        log_fail "large_dnode has state $state (expected active)"
fi

log_must zfs destroy $LDNFS

state=$(zpool list -Ho feature@large_dnode $LDNPOOL)
if [[ "$state" != "enabled" ]]; then
        log_fail "large_dnode has state $state (expected enabled)"
fi

log_pass
