#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
