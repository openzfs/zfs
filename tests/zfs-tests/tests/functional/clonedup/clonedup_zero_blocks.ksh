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
#	All-zero blocks become holes, the surviving copy included,
#	and no BRT entry is made for them.
#

verify_runnable "global"


log_assert "all-zero blocks become holes"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must dd if=/dev/zero of=/$TESTPOOL/z1 bs=$CD_BS count=$CD_BLOCKS \
    status=none
log_must dd if=/dev/zero of=/$TESTPOOL/z2 bs=$CD_BS count=$CD_BLOCKS \
    status=none
clonedup_sync
log_must [ $(du -k /$TESTPOOL/z1 | cut -f1) -gt 512 ]

clonedup_run
clonedup_sync
log_must [ $(du -k /$TESTPOOL/z1 | cut -f1) -lt 64 ]
log_must [ $(du -k /$TESTPOOL/z2 | cut -f1) -lt 64 ]
log_must [ $(zpool get -Hpo value bcloneused $TESTPOOL) -eq 0 ]
log_must cmp /$TESTPOOL/z1 /$TESTPOOL/z2
log_must [ $(stat_size /$TESTPOOL/z1) -eq $((CD_BLOCKS * CD_BS)) ]
clonedup_leakcheck

log_pass "all-zero blocks become holes"
