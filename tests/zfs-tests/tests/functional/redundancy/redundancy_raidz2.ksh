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
#	A raidz2 pool can withstand 2 devices are failing or missing.
#
# STRATEGY:
#	1. Create N(>3,<5) virtual disk files.
#	2. Create raidz2 pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged at most two of the virtual disk files.
#	6. Verify the data is correct to prove raidz2 can withstand 2 devices
#	   are failing.
#

verify_runnable "global"

log_assert "Verify raidz2 pool can withstand two devices failing."
log_onexit cleanup

typeset -i cnt=$(random_int_between 3 5)
setup_test_env $TESTPOOL raidz2 $cnt

#
# Inject data corruption errors for raidz2 pool
#
for i in 1 2; do
	damage_devs $TESTPOOL $i "label"
	log_must is_data_valid $TESTPOOL
	log_must clear_errors $TESTPOOL
done

#
# Inject bad devices errors for raidz2 pool
#
for i in 1 2; do
	damage_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

#
# Inject missing device errors for raidz2 pool
#
for i in 1 2; do
	remove_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

log_pass "raidz2 pool can withstand two devices failing passed."
