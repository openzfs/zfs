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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs hold' accepts a tag of up to 255 bytes, the longest name a ZAP
#	can store, and rejects a longer one with "tag too long" without
#	creating a hold, whatever state the snapshot's hold ZAP is in.
#
# STRATEGY:
#	1. Create a snapshot, and try to hold it with a 256-byte tag while it
#	   has no hold ZAP yet.
#	2. Hold it with a short tag, which makes its hold ZAP a microzap, and
#	   try a 256-byte tag again.
#	3. Hold it with a 255-byte tag, which upgrades the ZAP to a fat ZAP,
#	   check that the hold is listed, release it, and try 256-byte and
#	   1024-byte tags.
#	4. Check that only the short hold is left and that the pool still
#	   syncs.
#

verify_runnable "both"

typeset snap=$TESTPOOL/$TESTFS@$TESTSNAP
typeset tag255=$(printf "%0255d" 0 | tr 0 a)

function cleanup
{
	if snapexists $snap; then
		zfs release $tag255 $snap >/dev/null 2>&1
		zfs release short $snap >/dev/null 2>&1
		destroy_dataset $snap
	fi
}

function hold_too_long # length
{
	typeset tag=$(printf "%0${1}d" 0 | tr 0 a)
	typeset out

	out=$(zfs hold $tag $snap 2>&1) && \
	    log_fail "zfs hold with a $1-byte tag succeeded"
	[[ "$out" == *"tag too long"* ]] || \
	    log_fail "zfs hold with a $1-byte tag failed with: $out"
	log_note "zfs hold with a $1-byte tag failed: $out"
}

log_assert "'zfs hold' accepts tags of up to 255 bytes and rejects longer ones"
log_onexit cleanup

log_must zfs snapshot $snap
hold_too_long 256

log_must zfs hold short $snap
hold_too_long 256

log_must zfs hold $tag255 $snap
log_must eval "zfs holds -H $snap | grep -qF $tag255"
log_must zfs release $tag255 $snap
hold_too_long 256
hold_too_long 1024

log_must test "$(get_prop userrefs $snap)" -eq 1
sync_pool $TESTPOOL
log_must zfs release short $snap
log_must test "$(get_prop userrefs $snap)" -eq 0

log_pass "'zfs hold' accepts tags of up to 255 bytes and rejects longer ones"
