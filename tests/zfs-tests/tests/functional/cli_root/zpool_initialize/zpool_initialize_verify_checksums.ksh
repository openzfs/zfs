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
