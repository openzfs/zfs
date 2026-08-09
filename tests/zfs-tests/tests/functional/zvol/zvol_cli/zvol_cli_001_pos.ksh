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

#
# DESCRIPTION:
# Executing well-formed 'zfs list' commands should return success
#
# STRATEGY:
# 1. Create an array of valid options.
# 2. Execute each element in the array.
# 3. Verify success is returned.
#

verify_runnable "global"

TESTVOL='testvol'

set -A args  "list" "list -r" \
    "list $TESTPOOL/$TESTVOL" "list -r $TESTPOOL/$TESTVOL"  \
    "list -H $TESTPOOL/$TESTVOL" "list -Hr $TESTPOOL/$TESTVOL"  \
    "list -rH $TESTPOOL/$TESTVOL" "list -o name $TESTPOOL/$TESTVOL" \
    "list -r -o name $TESTPOOL/$TESTVOL" "list -H -o name $TESTPOOL/$TESTVOL" \
    "list -rH -o name $TESTPOOL/$TESTVOL"

log_assert "Executing well-formed 'zfs list' commands should return success"

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_must eval "zfs ${args[i]} > /dev/null"
	((i = i + 1))
done

log_pass "Executing zfs list on volume works as expected"
