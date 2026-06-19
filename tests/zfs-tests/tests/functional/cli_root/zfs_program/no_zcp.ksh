#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# STRATEGY:
#	1. Verify zfs channel program support is disabled
#

verify_runnable "both"

function cleanup
{
	return 0
}
log_onexit cleanup

log_assert "Channel programs are disabled"

TESTZCP="/$TESTPOOL/nosupp.zcp"
cat > "$TESTZCP" << EOF
	zfs.exists("$TESTPOOL")
EOF

# 1. Verify zfs channel program support is disabled
log_mustnot zfs program $TESTPOOL $TESTZCP 2>&1

log_pass "Channel programs are disabled"
