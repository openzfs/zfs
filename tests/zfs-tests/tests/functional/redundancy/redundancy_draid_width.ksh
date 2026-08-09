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
# Copyright (c) 2026 by Seagate Technology, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	A draid vdev with n failure groups can withstand n devices failing
#       or missing, each device being i-th one in each group.
#
# STRATEGY:
#	1. Create N(>3,<6) * n virtual disk files.
#	2. Create draid pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damage any n virtual disk files with the same offset in each group.
#	6. Verify the data is correct.
#

verify_runnable "global"

log_assert "Verify draid pool with n failure groups can withstand n i-th" \
	"devices failing in each group."
log_onexit cleanup

typeset -i children=$(random_int_between 3 6)
typeset -i fgroups=$(random_int_between 2 4)
typeset -i ith=$(random_int_between 0 $((children - 1)))
typeset -i width=$((children * fgroups))
setup_test_env $TESTPOOL draid:${children}c:${width}w $width

#
# Inject data corruption errors for draid pool
#
for (( i=0; i<$fgroups; i=i+1 )); do
	damage_devs_off $TESTPOOL 1 "$((ith + children*i))" "label"
done
log_must is_data_valid $TESTPOOL
log_must clear_errors $TESTPOOL

#
# Inject bad device errors for draid pool
#
for (( i=0; i<$fgroups; i=i+1 )); do
	damage_devs_off $TESTPOOL 1 "$((ith + children*i))"
done
log_must is_data_valid $TESTPOOL
log_must recover_bad_missing_devs $TESTPOOL 1

#
# Inject missing device errors for draid pool
#
for (( i=0; i<$fgroups; i=i+1 )); do
	remove_devs_off $TESTPOOL 1 "$((ith + children*i))"
done
log_must is_data_valid $TESTPOOL

log_pass "draid:${children}c:${width}w pool can withstand $fgroups i-th" \
	"devices failing passed."
