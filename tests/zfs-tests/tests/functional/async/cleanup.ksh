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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/async/async.cfg

# Before pool teardown, ensure all async ZIO completions have fired.
if tunable_exists ASYNC_DIO_ENABLED; then
	set_tunable32 ASYNC_DIO_ENABLED 0 2>/dev/null
	# Sync only if pool exists (may not if setup failed)
	zpool sync $TESTPOOL 2>/dev/null
fi

default_cleanup_noexit
log_pass
