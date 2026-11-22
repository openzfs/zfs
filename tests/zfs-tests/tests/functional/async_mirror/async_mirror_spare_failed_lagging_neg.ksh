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
#	ZED cannot replace a failed lagging child with a hot spare, and a
#	manual zpool replace with the spare is refused with a message that
#	tells the administrator to turn the async mirror off first.
#
# STRATEGY:
#	1. Start ZED and an async mirror with an available hot spare.
#	2. Fail writes to the lagging child, then make it unavailable.
#	3. Verify ZED cannot activate the spare and the child stays failed.
#	4. Verify that zpool replace with the spare fails, names the async
#	   mirror and leaves the spare available.
#

verify_runnable "global"

typeset spare=$ASYNC_MIRROR_DIR/spare
typeset before_digest
typeset failed_digest

function wait_zed_spare_refusal
{
	typeset -i i=0
	while (( i < 60 )); do
		grep -q 'no spare for' $ZED_DEBUG_LOG && return 0
		i=$((i + 1))
		sleep 1
	done
	return 1
}

function cleanup_async_mirror_spare_failed
{
	zed_stop
	zed_cleanup
	zinject -c all >/dev/null 2>&1
	cleanup_async_mirror
	log_must rm -f $spare
}

zed_setup
log_onexit cleanup_async_mirror_spare_failed
zed_start
zed_events_drain

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files before 2
before_digest="$(async_mirror_digests before 2)"
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $spare
log_must zpool add -f $TESTPOOL spare $spare
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files failed 1
failed_digest="$(async_mirror_digests failed 1)"
log_must wait_async_mirror_failed
log_must zinject -c all

log_must zinject -d $guid -e nxio -T all -f 100 $TESTPOOL
log_must zpool reopen $TESTPOOL
log_must wait_zed_spare_refusal
log_must wait_hotspare_state $TESTPOOL $spare AVAIL
log_must zinject -c all

log_must test "$(get_async_mirror_prop write_mode \
	$ASYNC_MIRROR_LAGGING)" == behind
log_must test "$(get_async_mirror_prop state \
	$ASYNC_MIRROR_LAGGING)" == failed
typeset status=$(zpool status -P $TESTPOOL)
echo "$status" | grep -Eq 'spare-[0-9]+' &&
	log_fail "ZED activated the hot spare"
log_must test "$before_digest" == "$(async_mirror_digests before 2)"
log_must test "$failed_digest" == "$(async_mirror_digests failed 1)"

typeset out
out=$(zpool replace $TESTPOOL $ASYNC_MIRROR_LAGGING $spare 2>&1) &&
    log_fail "zpool replace with a hot spare succeeded"
echo "$out" | grep -q "turn the mode off first" ||
    log_fail "the refusal does not tell what to do: $out"
log_must wait_hotspare_state $TESTPOOL $spare AVAIL

log_pass "A hot spare does not replace a failed lagging child."
