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
