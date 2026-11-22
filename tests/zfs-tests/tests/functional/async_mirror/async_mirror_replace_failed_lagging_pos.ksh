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
#	A failed lagging child can be replaced. The replacement stays in sync
#	mode after its resilver until the administrator enables the async
#	mirror again.
#
# STRATEGY:
#	1. Start an async mirror and write data.
#	2. Fail the replay to the lagging child.
#	3. Replace the failed child and check the status while it resilvers.
#	4. Verify that the replacement stays in sync mode. Then set it to
#	   behind and verify the async mirror starts.
#	5. Check the data and scrub the pool.
#

verify_runnable "global"

function cleanup_async_mirror_replace_failed
{
	cleanup_async_mirror
	log_must rm -f $TEST_BASE_DIR/async_mirror_replace_small.err \
	    $TEST_BASE_DIR/async_mirror_status.json
}

log_onexit cleanup_async_mirror_replace_failed

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files before 2
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files after 2
typeset digests="$(async_mirror_digests before 2)
$(async_mirror_digests after 2)"
log_must wait_async_mirror_failed
log_must zinject -c all

log_must truncate -s 1m $ASYNC_MIRROR_REPLACEMENT
typeset replace_error=$TEST_BASE_DIR/async_mirror_replace_small.err
if zpool replace -f $TESTPOOL $ASYNC_MIRROR_LAGGING \
    $ASYNC_MIRROR_REPLACEMENT >$replace_error 2>&1; then
	log_fail "a replacement smaller than the old leaf succeeded"
fi
grep -Fq 'device is too small' $replace_error ||
	log_fail "small replacement returned the wrong error" \
	    "$(cat $replace_error)"
log_must test "$(get_async_mirror_prop write_mode)" == behind
log_must test "$(get_async_mirror_prop state)" == failed
typeset status=$(zpool status -P $TESTPOOL)
if echo "$status" | grep -q \
    'async mirror: off (lagging child replaced)'; then
	log_fail "the failed small replacement switched the async mirror off"
fi

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_REPLACEMENT
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -f $TESTPOOL $ASYNC_MIRROR_LAGGING \
    $ASYNC_MIRROR_REPLACEMENT
log_must zpool sync -l $TESTPOOL
typeset status=$(zpool status -P $TESTPOOL)
echo "$status" | grep -q \
    'async mirror: off (lagging child replaced)' ||
	log_fail "The status does not report the async mirror as off"
if echo "$status" | grep -Eq 'lagging child( \([a-z-]+\))? at txg'; then
	log_fail "an off async mirror reported copy progress"
fi
typeset status_json=$TEST_BASE_DIR/async_mirror_status.json
log_must eval "zpool status -j --json-int $TESTPOOL > $status_json"
log_must jq -e --arg pool "$TESTPOOL" '
	.pools[$pool].async_mirror.replacing == 1
' "$status_json"

log_must zpool export $TESTPOOL
log_must async_mirror_import

log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop write_mode \
    $ASYNC_MIRROR_REPLACEMENT)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_REPLACEMENT
log_must wait_async_mirror_state "behind|current" $ASYNC_MIRROR_TIMEOUT \
	$ASYNC_MIRROR_REPLACEMENT
log_must wait_async_mirror_idle
if async_mirror_child_has_flag replace; then
	log_fail "the pool replacement marker remains after the replacement"
fi
async_mirror_write_files after_enable 1
typeset enabled_digest=$(async_mirror_digests after_enable 1)
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_prop write_mode \
    $ASYNC_MIRROR_REPLACEMENT)" == behind
log_must test "$digests" == "$(async_mirror_digests before 2)
$(async_mirror_digests after 2)"
log_must verify_pool $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests before 2)
$(async_mirror_digests after 2)"
log_must test "$enabled_digest" == \
	"$(async_mirror_digests after_enable 1)"

log_pass "A failed lagging child can be replaced and used again."
