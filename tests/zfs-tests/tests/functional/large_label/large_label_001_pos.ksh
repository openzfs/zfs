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
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2025 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/large_label/large_label.kshlib


#
# DESCRIPTION:
# Verify that new label works for basic pool operation.
#
# STRATEGY:
#	1. Create large virtual disk for the new label type
#	2. Create pool using large-label disk
#	3. Verify disks are using the new label format
#

function cleanup {
	log_pos zpool destroy $TESTPOOL
	log_must rm $mntpnt/dsk*
}

log_assert "Verify that new label works for basic pool operation"
log_onexit cleanup

mntpnt="$TESTDIR1"
log_must truncate -s 2T $mntpnt/dsk0

DSK="$mntpnt/dsk"

log_must create_pool -f $TESTPOOL "$DSK"0

log_must zdb -l "$DSK"0
log_must uses_large_label "$DSK"0
log_mustnot uses_old_label "$DSK"0

log_pass "New label works for basic pool operation"
