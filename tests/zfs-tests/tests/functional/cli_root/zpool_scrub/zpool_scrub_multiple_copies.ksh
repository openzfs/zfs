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
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Scrubs and self-healing should be able to repair data from additional
# copies that may be stored.
#
#
# STRATEGY:
# 1. Create a dataset with copies=3
# 2. Write a file to the dataset
# 3. zinject errors into the first and second DVAs of that file
# 4. Scrub and verify the scrub repaired all errors
# 7. Read the file normally to check that self healing also works
# 8. Remove the zinject handler
# 9. Scrub again and confirm 0 bytes were scrubbed
#

verify_runnable "global"

function cleanup
{
	destroy_dataset $TESTPOOL/$TESTFS2
	log_must zinject -c all
}
log_onexit cleanup

log_assert "Scrubs and self healing must work with additional copies"

log_must zfs create -o copies=3 $TESTPOOL/$TESTFS2
typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS2)
log_must mkfile 10m $mntpnt/file
sync_pool $TESTPOOL

log_must zinject -a -t data -C 0,1 -e io $mntpnt/file

log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must dd if=$mntpnt/file of=/dev/null bs=1M iflag=fullblock
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must zinject -c all

log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

zpool status

log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"

log_pass "Scrubs and self healing work with additional copies"
