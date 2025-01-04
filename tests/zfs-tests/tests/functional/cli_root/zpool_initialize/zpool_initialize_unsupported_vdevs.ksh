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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Attempting to initialize unsupported vdevs should fail.
#
# STRATEGY:
# 1. Create a pool with the following configuration:
#    root
#      mirror
#        vdev0
#        vdev1 (offline)
#      cache
#        vdev2
#      spare
#        vdev3
# 2. Try to initialize vdev1, vdev2, and vdev3. Ensure that all 3 fail.
#
function cleanup
{
        if datasetexists $TESTPOOL; then
                log_must zpool destroy -f $TESTPOOL
        fi
        if [[ -d $TESTDIR ]]; then
                log_must rm -rf $TESTDIR
        fi
}
log_onexit cleanup

log_must mkdir $TESTDIR
set -A FDISKS
for n in {0..2}; do
        log_must mkfile $MINVDEVSIZE $TESTDIR/vdev$n
        FDISKS+=("$TESTDIR/vdev$n")
done
FDISKS+=("${DISKS%% *}")

log_must zpool create $TESTPOOL mirror ${FDISKS[0]} ${FDISKS[1]} \
        spare ${FDISKS[2]} cache ${FDISKS[3]}

log_must zpool offline $TESTPOOL ${FDISKS[1]}

log_mustnot zpool initialize $TESTPOOL mirror-0
for n in {1..3}; do
        log_mustnot zpool initialize $TESTPOOL ${FDISKS[$n]}
done

log_pass "Attempting to initialize failed on unsupported devices"
