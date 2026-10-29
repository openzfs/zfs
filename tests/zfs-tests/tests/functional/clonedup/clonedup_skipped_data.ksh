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
#	Data that must not take part is left alone: encrypted
#	datasets, dedup=on datasets, blocks of different record size,
#	and the same data under different compression.
#

verify_runnable "global"

log_assert "encrypted, dedup, mismatched-size and" \
    "mismatched-compression data is skipped"
log_onexit clonedup_cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

# encrypted
log_must eval "echo 'password' | zfs create -o encryption=on" \
    "-o keyformat=passphrase $TESTPOOL/enc"
clonedup_write /$TESTPOOL/enc/a
clonedup_dup /$TESTPOOL/enc/a /$TESTPOOL/enc/b
# dedup=on: the DDT already shares these; clonedup must not touch them
log_must zfs create -o dedup=on $TESTPOOL/ddt
clonedup_write /$TESTPOOL/ddt/a
clonedup_dup /$TESTPOOL/ddt/a /$TESTPOOL/ddt/b
# the same blocks under the same checksum without dedup, which a
# walk that took dedup blocks would pair with ddt/a
log_must zfs create -o checksum=sha256 $TESTPOOL/sha
clonedup_dup /$TESTPOOL/ddt/a /$TESTPOOL/sha/c
# different record sizes never share a key, so this is a sanity check
log_must zfs create -o recordsize=64k $TESTPOOL/rs64
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/rs64/b
# the same blocks stored raw in a dataset that then sets lz4, so
# only the property check can refuse the pair
log_must zfs create $TESTPOOL/lz4
log_must eval "clonedup_compressible > /$TESTPOOL/plain"
clonedup_dup /$TESTPOOL/plain /$TESTPOOL/lz4/c
# a real pair so the run does some work
clonedup_dup /$TESTPOOL/a /$TESTPOOL/a2
clonedup_sync
log_must zfs set compression=lz4 $TESTPOOL/lz4

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL a2 "$all"
clonedup_check_shared $TESTPOOL/enc a $TESTPOOL/enc b "" password
clonedup_check_shared $TESTPOOL a $TESTPOOL/rs64 b ""
clonedup_check_shared $TESTPOOL plain $TESTPOOL/lz4 c ""
clonedup_check_shared $TESTPOOL/ddt a $TESTPOOL/sha c ""
# only a2 was a candidate
clonedup_jstat_is clonedup_candidates $CD_BLOCKS
clonedup_jstat_is clonedup_applied $CD_BLOCKS
# The compression check refused the lz4 copy, and ddt/a never
# reached the apply, so sha/c had nothing to pair with.
clonedup_jstat_is clonedup_skipped_policy $CD_BLOCKS
clonedup_leakcheck

log_pass "encrypted, dedup, mismatched-size and" \
    "mismatched-compression data is skipped"
