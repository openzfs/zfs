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
#	Files in an unmounted filesystem and blocks of a closed volume
#	are rewritten by owning the dataset.  The content is unchanged
#	once the filesystem is mounted and the volume opened again.
#	keep, the one file on a mounted filesystem, is written and
#	synced first, so it is the oldest copy and the source.
#

verify_runnable "global"

log_assert "unmounted filesystems and closed volumes are rewritten"
log_onexit clonedup_cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset nbytes=$((CD_BS * CD_BLOCKS))

clonedup_write /$TESTPOOL/keep
clonedup_sync
log_must zfs create $TESTPOOL/um
clonedup_dup /$TESTPOOL/keep /$TESTPOOL/um/a
clonedup_dup /$TESTPOOL/keep /$TESTPOOL/um/b
clonedup_sync
log_must zfs unmount $TESTPOOL/um

clonedup_zvol_create $TESTPOOL/cv
clonedup_zvol_fill /$TESTPOOL/keep $TESTPOOL/cv
clonedup_sync

clonedup_run
clonedup_check_shared $TESTPOOL/um a $TESTPOOL/um b "$all"
clonedup_check_shared_vol $TESTPOOL keep $TESTPOOL/cv "$all"
clonedup_jstat_is clonedup_skipped_busy 0
clonedup_kstat_gt dst_owned 0
clonedup_kstat_is dst_mounted 0
clonedup_jstat_is errors 0

log_must zfs mount $TESTPOOL/um
log_must cmp /$TESTPOOL/um/a /$TESTPOOL/um/b
log_must cmp /$TESTPOOL/um/a /$TESTPOOL/keep
log_must eval "clonedup_zvol_read $TESTPOOL/cv |" \
    "cmp -n $nbytes - /$TESTPOOL/keep"
clonedup_leakcheck

log_pass "unmounted filesystems and closed volumes are rewritten"
