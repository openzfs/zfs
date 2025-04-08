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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool replace' command with bad options fails.
#
# STRATEGY:
# 1. Create an array of badly formed 'zpool replace' options.
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

set -A args "" "-f" "-?" "-z fakepool" "-f fakepool" "-ev fakepool" "fakepool" \
        "$TESTPOOL" "-t $TESTPOOL/$TESTFS" "-t $TESTPOOL/$TESTFS $DISKLIST" \
        "$TESTPOOL/$TESTCTR" "-t $TESTPOOL/$TESTCTR/$TESTFS1" \
        "$TESTPOOL/$TESTCTR $DISKLIST" "-t $TESTPOOL/$TESTVOL" \
        "$TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
        "$TESTPOOL/$TESTVOL $DISKLIST" \
        "$DISKLIST" \
        "fakepool fakedevice" "fakepool fakedevice fakenewdevice" \
        "$TESTPOOL fakedevice" "$TESTPOOL $DISKLIST" \
	"$TESTPOOL fakedevice fakenewdevice fakenewdevice" \
        "-f $TESTPOOL" "-f $TESTPOOL/$TESTFS" "-f $TESTPOOL/$TESTFS $DISKLIST" \
        "-f $TESTPOOL/$TESTCTR" "-f $TESTPOOL/$TESTCTR/$TESTFS1" \
        "-f $TESTPOOL/$TESTCTR $DISKLIST" "-f $TESTPOOL/$TESTVOL" \
        "-f $TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
        "-f $TESTPOOL/$TESTVOL $DISKLIST" \
        "-f $DISKLIST" \
        "-f fakepool fakedevice" "-f fakepool fakedevice fakenewdevice" \
	"-f $TESTPOOL fakedevice fakenewdevice fakenewdevice" \
        "-f $TESTPOOL fakedevice" "-f $TESTPOOL $DISKLIST"

log_assert "Executing 'zpool replace' with bad options fails"

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0

while [[ $i -lt ${#args[*]} ]]; do

	log_mustnot zpool replace ${args[$i]}

	(( i = i + 1 ))
done

log_pass "'zpool replace' command with bad options failed as expected."
