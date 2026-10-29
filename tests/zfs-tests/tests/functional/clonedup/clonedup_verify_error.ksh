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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A verify read that fails leaves the pair alone.  The byte
#	compare is what proves two blocks are identical, so a read
#	that does not land means nothing was proved and nothing may be
#	cloned.  The failure is counted as an error and the count
#	reaches zpool status.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool clear $TESTPOOL >/dev/null 2>&1
	clonedup_cleanup
}

log_assert "a verify read that fails clones nothing and is counted"
log_onexit cleanup

clonedup_pool_create

clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Every data read of b fails, so whichever of the pair becomes the
# source, one side of the compare cannot be read.
log_must zinject -a -t data -e checksum -T read -f 100 /$TESTPOOL/b

clonedup_run

log_must zinject -c all
log_must zpool clear $TESTPOOL

clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
clonedup_jstat_is clonedup_applied 0
clonedup_jstat_gt errors 0
log_must eval \
    "zpool status $TESTPOOL | grep -qE 'with [1-9][0-9]* errors on'"

clonedup_leakcheck

log_pass "a verify read that fails clones nothing and is counted"
