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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

verify_runnable "global"

# The async mirror needs the test tunable and the pool feature.
tunable_exists ASYNC_MIRROR_SUSPEND_COPY ||
    log_unsupported "This system has no async mirror tunable"
zpool upgrade -v | grep -q async_mirror ||
    log_unsupported "This system has no async_mirror pool feature"

log_must mkdir -p $ASYNC_MIRROR_DIR

log_pass "Prepared the group of async mirror tests."
