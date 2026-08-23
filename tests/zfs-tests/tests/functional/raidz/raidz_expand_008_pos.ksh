#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
# shellcheck disable=SC2154
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#
# Copyright (c) 2026 Kamil Monicz. All rights reserved.
#

#
# DESCRIPTION:
#	Fast dedup must not add a copy allocated after a RAID-Z expansion to
#	a dedup entry written before it: a block pointer has one birth, from
#	which RAID-Z takes the width of every copy. Such a write must become
#	an ordinary block, while extending an entry within one width still
#	works.
#
# STRATEGY:
#	1. Before expansion, a second copy of deduplicated data extends its
#	   entry.
#	2. Write a 128K and a 16K block, expand the RAID-Z1 from three to four
#	   devices, and write each again with copies=2. Each must become an
#	   ordinary two-copy block at the new width. A 16K block allocates the
#	   same size at both widths but is laid out differently.
#	3. Scrub, and compare the data.
#	4. After expansion, a new entry can still be extended.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

typeset -r mnt="$TEST_BASE_DIR/raidz-fdt-mnt"
typeset -a disks

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"
	log_must rm -f "${disks[@]}"
	log_must rm -rf "$mnt"
	log_must restore_tunable SCRUB_AFTER_EXPAND
}

# Write $2 bytes to file $1, then a copy of it to file $3 with copies=2.
function write_pair
{
	log_must zfs set copies=1 "$TESTPOOL"
	log_must dd if=/dev/urandom of="$mnt/$1" bs="$2" count=1
	sync_pool "$TESTPOOL"
	log_must zfs set copies=2 "$TESTPOOL"
	log_must dd if="$mnt/$1" of="$mnt/$3" bs="$2" count=1
	sync_pool "$TESTPOOL"
	log_must cmp "$mnt/$1" "$mnt/$3"
}

# Require the L0 block pointer of file $1 to match $2.
function check_bp
{
	typeset bp
	bp=$(zdb -ddddddbbbbbb "$TESTPOOL/" "$(get_objnum "$mnt/$1")" |
	    grep -m 1 'L0 DVA')
	log_note "$1: $bp"
	[[ "$bp" =~ $2 ]] || log_fail "unexpected block pointer for $1"
}

log_assert "Fast dedup does not mix RAID-Z widths in one block pointer"
log_onexit cleanup
log_must save_tunable SCRUB_AFTER_EXPAND
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in 0 1 2 3; do
	disks[i]="$TEST_BASE_DIR/raidz-fdt-dev-$i"
	log_must truncate -s 256M "${disks[i]}"
done
log_must zpool create -f -o ashift=12 -o feature@fast_dedup=enabled \
    -o feature@raidz_expansion=enabled -O mountpoint="$mnt" \
    -O dedup=sha256 -O compression=off -O recordsize=128k \
    "$TESTPOOL" raidz1 "${disks[0]}" "${disks[1]}" "${disks[2]}"

write_pair before-1 128k before-2
check_bp before-2 'DVA\[0\]=<0:[0-9a-f]+:30000> DVA\[1\]=<0:[0-9a-f]+:30000> .* dedup double '

log_must zfs set copies=1 "$TESTPOOL"
log_must dd if=/dev/urandom of="$mnt/large-1" bs=128k count=1
log_must dd if=/dev/urandom of="$mnt/small-1" bs=16k count=1
sync_pool "$TESTPOOL"

log_must zpool attach "$TESTPOOL" raidz1-0 "${disks[3]}"
log_must zpool wait -t raidz_expand "$TESTPOOL"
# Blocks born from a few txgs after the expansion completes use the new width.
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"

log_must zfs set copies=2 "$TESTPOOL"
log_must dd if="$mnt/large-1" of="$mnt/large-2" bs=128k count=1
log_must dd if="$mnt/small-1" of="$mnt/small-2" bs=16k count=1
sync_pool "$TESTPOOL"
check_bp large-2 'DVA\[0\]=<0:[0-9a-f]+:2c000> DVA\[1\]=<0:[0-9a-f]+:2c000> .* unique double '
check_bp small-2 'DVA\[0\]=<0:[0-9a-f]+:6000> DVA\[1\]=<0:[0-9a-f]+:6000> .* unique double '

log_must zpool scrub -w "$TESTPOOL"
log_must check_pool_status "$TESTPOOL" "scan" "repaired 0B"
log_must check_pool_status "$TESTPOOL" "errors" "No known data errors"
log_must cmp "$mnt/large-1" "$mnt/large-2"
log_must cmp "$mnt/small-1" "$mnt/small-2"

write_pair after-1 128k after-2
check_bp after-2 'DVA\[0\]=<0:[0-9a-f]+:2c000> DVA\[1\]=<0:[0-9a-f]+:2c000> .* dedup double '

log_pass "Fast dedup does not mix RAID-Z widths in one block pointer"
