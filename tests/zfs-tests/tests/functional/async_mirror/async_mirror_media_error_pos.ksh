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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A media error on the leader, in a data block of the backlog, does
#	not end the async mirror.  The block is reported and skipped, and the copy
#	goes on.  The lagging child keeps its clean state: it does not
#	become a resilver target.
#
# STRATEGY:
#	1. Put the leader on a device-mapper device over a loop device.
#	   Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b.  Find its first data block on
#	   the leader.  Export, and make that range of the leader return
#	   I/O errors.
#	3. Restart the copy and import.  Wait until the lagging child has
#	   caught up.  Verify that the copy did not fail or freeze, that the
#	   kstat counts a data error, and that zpool status names b.
#	4. Export, and import the lagging child alone.  Verify files a, and
#	   that b cannot be read.
#

verify_runnable "global"

is_linux || log_unsupported "Needs device-mapper"
command -v dmsetup > /dev/null 2>&1 || log_unsupported "Needs dmsetup"

typeset dm=async_mirror_072
typeset dmdev=/dev/mapper/$dm
typeset loop=""
typeset -i sectors=0

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	dmsetup info $dm > /dev/null 2>&1 && log_must dmsetup remove $dm
	[[ -n "$loop" ]] && log_must losetup -d $loop
	cleanup_async_mirror
}

#
# Load a table that maps the whole leader file, or that returns I/O errors
# for one range of it.
#
# $1 first sector of the bad range, or nothing for no bad range
# $2 length of the bad range, in sectors
#
function leader_table # [start length]
{
	typeset -i start=${1:-0} len=${2:-0}

	log_must dmsetup suspend $dm
	if (( len == 0 )); then
		log_must eval "echo '0 $sectors linear $loop 0' | dmsetup load $dm"
	else
		log_must eval "printf '%s\n' '0 $start linear $loop 0' \
		    '$start $len error' \
		    '$((start + len)) $((sectors - start - len)) linear $loop \
		    $((start + len))' | dmsetup load $dm"
	fi
	log_must dmsetup resume $dm
}

function import_both
{
	log_must zpool import -d /dev/mapper -d $ASYNC_MIRROR_DIR $TESTPOOL
}

log_onexit cleanup

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_LEADER \
    $ASYNC_MIRROR_LAGGING
loop=$(losetup -f --show $ASYNC_MIRROR_LEADER)
[[ -n "$loop" ]] || log_fail "No loop device for the leader"
sectors=$(blockdev --getsz $loop)
log_must dmsetup create $dm --table "0 $sectors linear $loop 0"
block_device_wait $dmdev
log_must zpool create -f $TESTPOOL mirror $dmdev $ASYNC_MIRROR_LAGGING
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
import_both
log_must test "$(get_async_mirror_stat active)" == 1
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 1

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
typeset level path offset length found=""
list_file_blocks /$TESTPOOL/b | while read level path offset length; do
	[[ $level == "L0" && $path == "$dmdev" ]] || continue
	found="$offset $length"
	break
done
[[ -n "$found" ]] || log_fail "No L0 block of b on the leader"
log_note "bad range of the leader: $found"
log_must zpool export $TESTPOOL
leader_table $found

async_mirror_suspend_copy 0
import_both
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat active)" == 1
log_must test "$(get_async_mirror_stat data_errors)" -ge 1
log_must eval "zpool status -v $TESTPOOL | grep -q '/$TESTPOOL/b\$'"
log_must zpool export $TESTPOOL
leader_table

log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot eval "cat /$TESTPOOL/b > /dev/null"
log_must zpool export $TESTPOOL

log_pass "A data block with a media error is reported and skipped."
