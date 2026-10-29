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
#	Cross-dataset pairs obey zfs_bclone_strict_properties: a
#	differing copies= property is refused with the tunable set and
#	cloned without it.
#
#	The apply always refuses a pair whose block pointers hold
#	different DVA counts, since the clone installs the source
#	pointer whole.  The test writes both files at copies=2 and
#	lowers one dataset's property afterwards, so only the
#	property comparison can refuse the pair.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable BCLONE_STRICT_PROPERTIES
	clonedup_cleanup
}

log_assert "cross-dataset clones honor zfs_bclone_strict_properties"
log_onexit cleanup
log_must save_tunable BCLONE_STRICT_PROPERTIES

clonedup_pool_create
log_must zfs create -o copies=2 $TESTPOOL/one
log_must zfs create -o copies=2 $TESTPOOL/two
clonedup_write /$TESTPOOL/one/a
clonedup_dup /$TESTPOOL/one/a /$TESTPOOL/two/b
clonedup_sync
# both files now hold two DVAs a block; lower one property so the
# datasets disagree while the block pointers do not
log_must zfs set copies=1 $TESTPOOL/one

log_must set_tunable32 BCLONE_STRICT_PROPERTIES 1
clonedup_run
clonedup_check_shared $TESTPOOL/one a $TESTPOOL/two b ""
clonedup_jstat_gt clonedup_skipped_policy 0

log_must set_tunable32 BCLONE_STRICT_PROPERTIES 0
clonedup_run -f
clonedup_check_shared $TESTPOOL/one a $TESTPOOL/two b \
    "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "cross-dataset clones honor zfs_bclone_strict_properties"
