#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025 by Klara Inc.
#

#
# Description:
# Verify that gang block functionality behaves correctly.
#
# Strategy:
# 1. Create a pool without dynamic gang headers.
# 2. Set metaslab_force_ganging to force gang blocks to be created.
# 3. Verify that gang blocks can be read, written, and freed.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Gang blocks behave correctly."

preamble
log_onexit cleanup

log_must zpool create -f $TESTPOOL $DISKS
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
set_tunable64 METASLAB_FORCE_GANGING 100000
set_tunable32 METASLAB_FORCE_GANGING_PCT 100

path="${mountpoint}/file"
log_must dd if=/dev/urandom of=$path bs=128k count=1
log_must zpool sync $TESTPOOL
first_block=$(get_first_block_dva $TESTPOOL/$TESTFS file)
leaves=$(read_gang_header $TESTPOOL $first_block 200 | grep -v hole | wc -l)
[[ "$leaves" -gt 1 ]] || log_fail "Only one leaf in gang block, should not be possible"

orig_checksum="$(cat $path | xxh128digest)"

log_must verify_pool $TESTPOOL
log_must zinject -a
new_checksum="$(cat $path | xxh128digest)"
[[ "$orig_checksum" == "$new_checksum" ]] || log_fail "Checksum mismatch"

log_must rm $path
log_must verify_pool $TESTPOOL

log_pass "Gang blocks behave correctly."
