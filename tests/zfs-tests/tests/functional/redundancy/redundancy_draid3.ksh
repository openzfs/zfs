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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	A draid3 pool can withstand 3 devices are failing or missing.
#
# STRATEGY:
#	1. Create N(>5,<6) virtual disk files.
#	2. Create draid3 pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged at most three of the virtual disk files.
#	6. Verify the data is correct to prove draid3 can withstand 3 devices
#	   are failing.
#

verify_runnable "global"

log_assert "Verify draid3 pool can withstand three devices failing."
log_onexit cleanup

typeset -i cnt=$(random_int_between 5 6)
setup_test_env $TESTPOOL draid3 $cnt

#
# Inject data corruption errors for draid3 pool
#
for i in 1 2 3; do
	damage_devs $TESTPOOL $i "label"
	log_must is_data_valid $TESTPOOL
	log_must clear_errors $TESTPOOL
done

#
# Inject bad devices errors for draid3 pool
#
for i in 1 2 3; do
	damage_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

#
# Inject missing device errors for draid3 pool
#
for i in 1 2 3; do
	remove_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

log_pass "draid3 pool can withstand three devices failing passed."
