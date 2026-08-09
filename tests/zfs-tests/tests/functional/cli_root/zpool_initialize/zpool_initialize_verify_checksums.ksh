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
# Initializing does not cause file corruption.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Write data to the pool.
# 3. Start initializing and verify that initializing is active.
# 4. Write more data to the pool.
# 5. Run zdb to validate checksums.
#

DISK1=${DISKS%% *}

log_must zpool create -f $TESTPOOL $DISK1
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=30
sync_all_pools

log_must zpool initialize $TESTPOOL

log_must zdb -cc $TESTPOOL

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Initializing did not start"

log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=30
sync_all_pools

log_must zdb -cc $TESTPOOL

log_pass "Initializing does not corrupt existing or new data"
