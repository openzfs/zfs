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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool offline' command with bad options fails.
#
# STRATEGY:
# 1. Create an array of badly formed 'zpool offline' options.
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

set -A args "" "-?" "-t fakepool" "-f fakepool" "-ev fakepool" "fakepool" \
	"-t $TESTPOOL" "-t $TESTPOOL/$TESTFS" "-t $TESTPOOL/$TESTFS $DISKLIST" \
	"-t $TESTPOOL/$TESTCTR" "-t $TESTPOOL/$TESTCTR/$TESTFS1" \
	"-t $TESTPOOL/$TESTCTR $DISKLIST" "-t $TESTPOOL/$TESTVOL" \
	"-t $TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
	"-t $TESTPOOL/$TESTVOL $DISKLIST" \
	"-t $DISKLIST" \
        "-f $TESTPOOL" "-f $TESTPOOL/$TESTFS" "-f $TESTPOOL/$TESTFS $DISKLIST" \
        "-f $TESTPOOL/$TESTCTR" "-f $TESTPOOL/$TESTCTR/$TESTFS1" \
        "-f $TESTPOOL/$TESTCTR $DISKLIST" "-f $TESTPOOL/$TESTVOL" \
        "-f $TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
        "-f $TESTPOOL/$TESTVOL $DISKLIST" \
        "-f $DISKLIST" \
        "-ft $TESTPOOL" "-ft $TESTPOOL/$TESTFS" \
	"-ft $TESTPOOL/$TESTFS $DISKLIST" \
        "-ft $TESTPOOL/$TESTCTR" "-ft $TESTPOOL/$TESTCTR/$TESTFS1" \
        "-ft $TESTPOOL/$TESTCTR $DISKLIST" "-ft $TESTPOOL/$TESTVOL" \
        "-ft $TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
        "-ft $TESTPOOL/$TESTVOL $DISKLIST" \
        "-ft $DISKLIST" \
        "-tf $TESTPOOL" "-tf $TESTPOOL/$TESTFS" \
	"-tf $TESTPOOL/$TESTFS $DISKLIST" \
        "-tf $TESTPOOL/$TESTCTR" "-tf $TESTPOOL/$TESTCTR/$TESTFS1" \
        "-tf $TESTPOOL/$TESTCTR $DISKLIST" "-tf $TESTPOOL/$TESTVOL" \
        "-tf $TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" \
        "-tf $TESTPOOL/$TESTVOL $DISKLIST" \
        "-tf $DISKLIST" \
	"$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTFS $DISKLIST" \
	"$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTCTR/$TESTFS1" \
	"$TESTPOOL/$TESTCTR $DISKLIST" "$TESTPOOL/$TESTVOL" \
	"$TESTPOOL/$TESTCTR/$TESTFS1 $DISKLIST" "$TESTPOOL/$TESTVOL $DISKLIST" \
	"$DISKLIST"

log_assert "Executing 'zpool offline' with bad options fails"

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0

while [[ $i -lt ${#args[*]} ]]; do

	log_mustnot zpool offline ${args[$i]}

	(( i = i + 1 ))
done

log_pass "'zpool offline' command with bad options failed as expected."
