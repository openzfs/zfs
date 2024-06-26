#!/bin/ksh -p
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
# Copyright (c) 2024, Klara, Inc.
#

#
# This tests that when a pool is degraded, ZIL writes/flushes are still done
# directly, rather than falling back to a full txg flush.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

function cleanup {
	default_cleanup_noexit
}

log_onexit cleanup

log_assert "ZIL writes go direct to the pool even when the pool is degraded"

function zil_commit_count {
  kstat zil | grep ^zil_commit_count | awk '{ print $3 }'
}
function zil_commit_error_count {
  kstat zil | grep ^zil_commit_error_count | awk '{ print $3 }'
}

DISK1=${DISKS%% *}
log_must default_mirror_setup_noexit $DISKS

# get the current count of commits vs errors
typeset -i c1=$(zil_commit_count)
typeset -i e1=$(zil_commit_error_count)

# force a single ZIL commit
log_must dd if=/dev/zero of=/$TESTPOOL/file bs=128k count=1 conv=fsync

# get the updated count of commits vs errors
typeset -i c2=$(zil_commit_count)
typeset -i e2=$(zil_commit_error_count)

# degrade the pool
log_must zpool offline -f $TESTPOOL $DISK1
log_must wait_for_degraded $TESTPOOL

# force another ZIL commit
log_must dd if=/dev/zero of=/$TESTPOOL/file bs=128k count=1 conv=fsync

# get counts again
typeset -i c3=$(zil_commit_count)
typeset -i e3=$(zil_commit_error_count)

# repair the pool
log_must zpool online $TESTPOOL $DISK1

# when pool is in good health, a ZIL commit should go direct to the
# pool and not fall back to a txg sync
log_must test $(( $c2 - $c1 )) -eq 1
log_must test $(( $e2 - $e1 )) -eq 0

# when pool is degraded but still writeable, ZIL should still go direct
# to the pull and not fall back
log_must test $(( $c3 - $c2 )) -eq 1
log_must test $(( $e3 - $e2 )) -eq 0

log_pass "ZIL writes go direct to the pool even when the pool is degraded"
