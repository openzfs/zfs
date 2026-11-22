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
#	"zpool import --async-mirror-keep" discards one copy of the data, so
#	it needs one pool and one positive guid.  The command refuses the
#	option with -a, with no pool argument, and with a negative or
#	non-numeric guid, and it says why.
#
# STRATEGY:
#	1. Create a pool with an async mirror and export it.
#	2. Verify that each wrong form of the option is refused, with a clear
#	   message, and that no pool is imported.
#	3. Verify that the option with the guid of a child and one named pool
#	   is accepted.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

function keep_refused # message option ...
{
	typeset msg=$1
	typeset out

	shift
	out=$(zpool import -d $ASYNC_MIRROR_DIR "$@" 2>&1) &&
	    log_fail "zpool import $* succeeded"
	log_note "$out"
	echo "$out" | grep -q -- "$msg" ||
	    log_fail "zpool import $*: no message '$msg'"
	log_mustnot poolexists $TESTPOOL
}

async_mirror_create_pool
async_mirror_enable
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must test -n "$guid"
log_must zpool export $TESTPOOL

keep_refused "incompatible with -a" -a --async-mirror-keep=$guid
keep_refused "needs the name or guid of one pool" --async-mirror-keep=$guid
keep_refused "invalid guid" --async-mirror-keep=-5 $TESTPOOL
keep_refused "invalid guid" --async-mirror-keep=-$guid $TESTPOOL
keep_refused "invalid guid" "--async-mirror-keep= $guid" $TESTPOOL
keep_refused "invalid guid" --async-mirror-keep=0 $TESTPOOL
keep_refused "invalid guid" --async-mirror-keep=abc $TESTPOOL

# The right form still works: the copies were not written separately.
log_must zpool import -d $ASYNC_MIRROR_DIR --async-mirror-keep=$guid \
    $TESTPOOL
log_must poolexists $TESTPOOL

log_pass "The keep option needs one pool and one positive guid."
