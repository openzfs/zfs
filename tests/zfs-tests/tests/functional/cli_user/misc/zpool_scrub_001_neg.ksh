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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zpool scrub returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to start a scrub on a pool
# 2. Verify the command fails
#
#

verify_runnable "global"

log_assert "zpool scrub returns an error when run as a user"

log_mustnot zpool scrub $TESTPOOL
log_mustnot zpool scrub -p $TESTPOOL
log_mustnot zpool scrub -s $TESTPOOL

log_pass "zpool scrub returns an error when run as a user"
