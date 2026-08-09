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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zpool list' succeeds as non-root.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify the command succeeds.
#

verify_runnable "both"

if ! is_global_zone; then
	TESTPOOL=${TESTPOOL%%/*}
fi

set -A args "list $TESTPOOL" "list -H $TESTPOOL" "list" "list -H" \
    "list -H -o name $TESTPOOL" "list -o name $TESTPOOL" \
    "list -o name,size,capacity,health,altroot $TESTPOOL" \
    "list -H -o name,size,capacity,health,altroot $TESTPOOL" \
    "list -o alloc,free $TESTPOOL"
log_assert "zpool list [-H] [-o filed[,filed]*] [<pool_name> ...]"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_must zpool ${args[i]}

	((i = i + 1))
done

log_pass "The sub-command 'list' succeeds as non-root."
