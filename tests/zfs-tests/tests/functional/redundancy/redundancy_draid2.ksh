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
#	A draid2 pool can withstand 2 devices are failing or missing.
#
# STRATEGY:
#	1. Create N(>4,<6) virtual disk files.
#	2. Create draid2 pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged at most two of the virtual disk files.
#	6. Verify the data is correct to prove draid2 can withstand 2 devices
#	   are failing.
#

verify_runnable "global"

log_assert "Verify draid2 pool can withstand two devices failing."
log_onexit cleanup

typeset -i cnt=$(random_int_between 4 6)
setup_test_env $TESTPOOL draid2 $cnt

#
# Inject data corruption errors for draid2 pool
#
for i in 1 2; do
	damage_devs $TESTPOOL $i "label"
	log_must is_data_valid $TESTPOOL
	log_must clear_errors $TESTPOOL
done

#
# Inject bad devices errors for draid2 pool
#
for i in 1 2; do
	damage_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

#
# Inject missing device errors for draid2 pool
#
for i in 1 2; do
	remove_devs $TESTPOOL $i
	log_must is_data_valid $TESTPOOL
	log_must recover_bad_missing_devs $TESTPOOL $i
done

log_pass "draid2 pool can withstand two devices failing passed."
