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
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that a spill block which is stored as an embedded block pointer is
# still sent as a DRR_SPILL record.
#
# Strategy:
# 1. Create a file whose SA xattrs need a spill block, growing a single
#    highly compressible xattr until that spill block is embedded.
# 2. Send with -e, and verify the stream has a DRR_SPILL record for the
#    object and no DRR_WRITE_EMBEDDED record for it.
# 3. Verify the received copy has identical contents and xattrs.
#

verify_runnable "both"

log_assert "Embedded spill blocks are sent as DRR_SPILL records"
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset stream=$BACKDIR/stream
typeset dump=$BACKDIR/dump
typeset zdbout=$BACKDIR/zdb.out

log_must zfs create -o xattr=sa -o dnodesize=legacy -o compression=zstd $sendfs
typeset senddir=$(get_prop mountpoint $sendfs)
typeset file=$senddir/spill

log_must mkfile 16384 $file
typeset -i obj=$(get_objnum $file)

#
# Grow a single repeated-byte xattr until the spill block compresses to within
# BPE_PAYLOAD_SIZE.
#
typeset value
value=$(awk 'BEGIN { while (n++ < 512) printf "a" }')
typeset -i i
typeset embedded="false"
for (( i = 0; i < 6; i++ )); do
	log_must set_xattr big "$value" $file
	sync_pool $POOL2
	log_must eval "zdb -ddddd $sendfs $obj >$zdbout"
	if grep -q "Spill block:.*EMBEDDED" $zdbout; then
		embedded="true"
		break
	fi
	value="$value$value"
done

if [[ $embedded != "true" ]]; then
	if ! grep -q SPILL_BLKPTR $zdbout; then
		log_fail "no SA spill block was created for object $obj"
	fi
	log_note "spill for object $obj: $(grep 'Spill block:' $zdbout)"
	log_fail "no embedded spill block was created"
fi
log_note "embedded spill block created with a ${#value} byte xattr"

log_must zfs snapshot $sendfs@snap
log_must eval "zfs send -e $sendfs@snap >$stream"
log_must eval "zstream dump -v <$stream >$dump"

log_must grep -q "SPILL block for object = $obj " $dump

typeset bad=$(awk -v obj=$obj \
    '$1 == "WRITE_EMBEDDED" && $4 == obj { print }' $dump)
[[ -z $bad ]] || log_fail "spill block sent as WRITE_EMBEDDED: $bad"

log_must eval "zfs recv $recvfs <$stream"

# The recursive checksum covers the file contents and its xattrs.
typeset expected=$(recursive_cksum $senddir)
typeset actual=$(recursive_cksum $(get_prop mountpoint $recvfs))
[[ "$expected" == "$actual" ]] || \
    log_fail "Checksums differ ($expected != $actual)"

log_pass "Embedded spill blocks are sent as DRR_SPILL records"
