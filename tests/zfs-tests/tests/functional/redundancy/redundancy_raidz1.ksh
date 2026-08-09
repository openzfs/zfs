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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	A raidz pool can withstand at most 1 device failing or missing.
#
# STRATEGY:
#	1. Create N(>2,<5) virtual disk files.
#	2. Create raidz pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged one of the virtual disk file.
#	6. Verify the data is correct to prove raidz can withstand 1 device is
#	   failing.
#

verify_runnable "global"

log_assert "Verify raidz pool can withstand one device failing."
log_onexit cleanup

typeset -i cnt=$(random_int_between 2 5)
setup_test_env $TESTPOOL raidz $cnt

#
# Inject data corruption error for raidz pool
#
damage_devs $TESTPOOL 1 "label"
log_must is_data_valid $TESTPOOL
log_must clear_errors $TESTPOOL

#
# Inject bad device error for raidz pool
#
damage_devs $TESTPOOL 1
log_must is_data_valid $TESTPOOL
log_must recover_bad_missing_devs $TESTPOOL 1

#
# Inject missing device error for raidz pool
#
remove_devs $TESTPOOL 1
log_must is_data_valid $TESTPOOL

log_pass "raidz pool can withstand one device failing passed."
