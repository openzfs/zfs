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
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	Striped pool have no data redundancy. Any device errors will
#	cause data corruption.
#
# STRATEGY:
#	1. Create N virtual disk file.
#	2. Create stripe pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damage one of the virtual disk file.
#	6. Verify the data is error.
#

verify_runnable "global"

log_assert "Verify striped pool have no data redundancy."
log_onexit cleanup

typeset -i cnt=$(random_int_between 2 5)
setup_test_env $TESTPOOL "" $cnt

damage_devs $TESTPOOL 1 "keep_label"
log_must zpool scrub -w $TESTPOOL

if is_healthy $TESTPOOL ; then
	log_fail "$pool should not be healthy."
fi

log_pass "Striped pool has no data redundancy as expected."
