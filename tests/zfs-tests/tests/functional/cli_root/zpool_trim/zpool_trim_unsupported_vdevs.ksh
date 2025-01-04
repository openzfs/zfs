#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# Attempting to trim unsupported vdevs should fail.
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
# 2. Try to trim vdev1, vdev2, and vdev3. Ensure that all 3 fail.
#
function cleanup
{
        if datasetexists $TESTPOOL; then
                destroy_pool $TESTPOOL
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

log_mustnot zpool trim $TESTPOOL mirror-0
for n in {1..3}; do
        log_mustnot zpool trim $TESTPOOL ${FDISKS[$n]}
done

log_pass "Attempting to trim failed on unsupported devices"
