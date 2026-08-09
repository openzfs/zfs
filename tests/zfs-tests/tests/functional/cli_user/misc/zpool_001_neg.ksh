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
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zpool shows a usage message when run as a user
#
# STRATEGY:
# 1. Run the zpool command
# 2. Verify that a usage message is produced
#
#

function cleanup
{
	if [ -e "$TEMPFILE" ]
	then
		rm -f "$TEMPFILE"
	fi
}

TEMPFILE="$TEST_BASE_DIR/zpool_001_neg.$$.txt"

log_onexit cleanup
log_assert "zpool shows a usage message when run as a user"

eval "zpool > $TEMPFILE 2>&1"
log_must grep "usage: zpool command args" "$TEMPFILE"

log_must awk '{if (length($0) > 80) exit 1}' $TEMPFILE

log_pass "zpool shows a usage message when run as a user"
