#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Returning very large (up to the memory limit) lists should
# function correctly.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild

function cleanup
{
	datasetexists $fs && log_must zfs destroy -R $fs
}

log_onexit cleanup

log_must zfs create $fs

#
# Actually checking in the ~500kb expected result of this program would be
# awful, so we just make sure it was as long as we expected.
#
output_lines=$(log_must zfs program $TESTPOOL \
    $ZCP_ROOT/lua_core/tst.return_large.zcp | wc -l)

[[ $output_lines -lt 5000 ]] &&
    log_fail "Expected return of full list but only got $output_lines lines"

#
# Make sure we fail if the return is over the memory limit
#
log_mustnot_program $TESTPOOL -m 10000 \
    $ZCP_ROOT/lua_core/tst.return_large.zcp

log_pass "Large return values work properly"

