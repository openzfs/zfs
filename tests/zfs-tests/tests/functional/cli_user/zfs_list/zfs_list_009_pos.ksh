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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	Verify 'zfs list -o+' allows users to append a column to the defaults
#
# STRATEGY:
# 1. Add a user comment to dataset
# 2. Execute `zfs list -o +comment:`.
# 3. Verify the first column of the defaults gets printed.
# 4. Verify the user comment appears as the last column.
# 5. Verify we see both one of the default entries and the user comment in JSON.

verify_runnable "both"

log_assert "Verify 'zfs list -o+<...>' appends columns to the defaults."

log_must zfs set comment:=helloworld $TESTPOOL

log_must eval zfs list $TESTPOOL -o +comment: | grep -Eq '^NAME.+COMMENT:$'
log_must eval zfs list $TESTPOOL -o +comment: | grep -Eq "^$TESTPOOL.+helloworld$"
val=$(zfs list -j -o +comment: $TESTPOOL | jq -r '.datasets.'$TESTPOOL'.properties."comment:".value')
+log_must test $val == "helloworld"
val=$(zfs list -j -o +comment: $TESTPOOL | jq -r ".datasets.$TESTPOOL.properties.mountpoint.value")
+log_must test $val == "/$TESTPOOL"

log_pass "'zfs list -o+<...>' successfully added columns to the defaults."
