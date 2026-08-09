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
