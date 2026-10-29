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
#	Open volumes are rewritten like files: a volume that
#	duplicates a file and a second volume is redirected onto the
#	file's blocks, a file that duplicates an older volume is
#	redirected onto the volume, and the content read back through
#	the device is unchanged.
#

verify_runnable "global"

function cleanup
{
	exec 3<&- 4<&- 5<&-
	rm -f $TEST_BASE_DIR/clonedup_vol
	clonedup_cleanup
}

log_assert "open volumes are valid destinations and sources"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset nbytes=$((CD_BS * CD_BLOCKS))

# file first, then two volumes with the same content
clonedup_write /$TESTPOOL/a
clonedup_sync
clonedup_zvol_create $TESTPOOL/v1
clonedup_zvol_create $TESTPOOL/v2
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/v1
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/v2

# volume first, then a file with the same content
clonedup_zvol_create $TESTPOOL/old
log_must dd if=/dev/urandom of=$TEST_BASE_DIR/clonedup_vol bs=$CD_BS \
    count=$CD_BLOCKS status=none
clonedup_zvol_fill $TEST_BASE_DIR/clonedup_vol $TESTPOOL/old
clonedup_sync
clonedup_dup $TEST_BASE_DIR/clonedup_vol /$TESTPOOL/b
clonedup_sync

typeset oldblocks="$(clonedup_vol_blocks $TESTPOOL/old)"
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/v1 ""
clonedup_check_shared_vol $TESTPOOL b $TESTPOOL/old ""

# keep every volume open for the run
exec 3<> $ZVOL_DEVDIR/$TESTPOOL/v1
exec 4<> $ZVOL_DEVDIR/$TESTPOOL/v2
exec 5<> $ZVOL_DEVDIR/$TESTPOOL/old

clonedup_run
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/v1 "$all"
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/v2 "$all"
clonedup_check_shared_vol $TESTPOOL b $TESTPOOL/old "$all"
log_must [ "$(clonedup_vol_blocks $TESTPOOL/old)" = "$oldblocks" ]
clonedup_jstat_is clonedup_applied $((3 * CD_BLOCKS))
clonedup_kstat_gt dst_zvol 0
clonedup_jstat_is clonedup_skipped_busy 0
clonedup_jstat_is errors 0

exec 3<&- 4<&- 5<&-
log_must eval \
    "clonedup_zvol_read $TESTPOOL/v1 | cmp -n $nbytes - /$TESTPOOL/a"
log_must eval \
    "clonedup_zvol_read $TESTPOOL/v2 | cmp -n $nbytes - /$TESTPOOL/a"
log_must eval \
    "clonedup_zvol_read $TESTPOOL/old | cmp -n $nbytes - /$TESTPOOL/b"
clonedup_leakcheck

log_pass "open volumes are valid destinations and sources"
