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
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Starting and stopping an initialize works.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Cancel initializing and verify that initializing is not active.
#

DISK1=${DISKS%% *}

log_must zpool create -f $TESTPOOL $DISK1
log_must zpool initialize $TESTPOOL

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
    log_fail "Initialize did not start"

log_must zpool initialize -c $TESTPOOL

[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] || \
    log_fail "Initialize did not stop"

log_pass "Initialize start + cancel works"
