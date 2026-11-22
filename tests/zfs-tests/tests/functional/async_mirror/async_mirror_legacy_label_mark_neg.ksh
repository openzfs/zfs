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

verify_runnable "global"

typeset err_file=$TEST_BASE_DIR/async_mirror_legacy_mark_err.$$
typeset want

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && poolexists $TESTPOOL && \
	    zpool export $TESTPOOL
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $err_file
}

log_onexit cleanup
log_assert "A child with a legacy async mirror label mark cannot be" \
    "imported alone as a writable pool."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files keep 2
log_must wait_async_mirror_idle
want=$(async_mirror_digests keep 2)
log_must zpool export $TESTPOOL

# The test tool writes a label made by a later implementation. The core
# itself does not write this key.
log_must zhack label async-mirror-legacy-mark $ASYNC_MIRROR_LAGGING
log_must async_mirror_label_has_legacy_mark $ASYNC_MIRROR_LAGGING
log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN

log_mustnot eval "zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2> $err_file"
log_must grep -q 'was catching up and is not consistent' $err_file
log_mustnot eval "zpool import -o readonly=on -d $ASYNC_MIRROR_DIR \
    $TESTPOOL 2> $err_file"
log_must grep -q 'was catching up and is not consistent' $err_file
log_mustnot eval "zpool import -f -d $ASYNC_MIRROR_DIR $TESTPOOL 2> $err_file"
log_must grep -q 'was catching up and is not consistent' $err_file

# A forced read-only import is allowed so that the owner can rescue data.
log_must zpool import -f -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_pool_prop readonly $TESTPOOL)" == on
log_must test "$want" == "$(async_mirror_digests keep 2)"
log_must zpool export $TESTPOOL
log_must async_mirror_label_has_legacy_mark $ASYNC_MIRROR_LAGGING

# The mark does not stop a full pool import when the other copy is present.
async_mirror_unhide_leader
async_mirror_import
log_must test "$(get_async_mirror_prop)" == behind
log_must wait_async_mirror_idle
log_must test "$want" == "$(async_mirror_digests keep 2)"
log_must verify_pool $TESTPOOL

log_pass "A legacy label mark refuses an alone writable import."
