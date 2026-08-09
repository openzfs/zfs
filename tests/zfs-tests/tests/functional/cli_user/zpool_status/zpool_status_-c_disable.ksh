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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify zpool status command mode (-c) respects ZPOOL_SCRIPTS_ENABLED.
#
# STRATEGY:
#	1. Set ZPOOL_SCRIPTS_ENABLED to 0, disabling zpool status -c
#	2. zpool status -c must not run successfully
#	3. Set ZPOOL_SCRIPTS_ENABLED to 1, enabling zpool status -c
#	4. zpool status -c must run successfully
#	5. Unset ZPOOL_SCRIPTS_ENABLED, enabling zpool status -c
#	6. zpool status -c must run successfully

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

log_assert "zpool status -c properly handles ZPOOL_SCRIPTS_ENABLED"

export ZPOOL_SCRIPTS_ENABLED=0
log_mustnot zpool status -c media

export ZPOOL_SCRIPTS_ENABLED=1
log_must zpool status -c media

unset ZPOOL_SCRIPTS_ENABLED
log_must zpool status -c media

log_pass "zpool status -c properly handles ZPOOL_SCRIPTS_ENABLED passed"
