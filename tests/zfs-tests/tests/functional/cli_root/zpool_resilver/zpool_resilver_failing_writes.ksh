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

# shellcheck disable=SC1091
. "$STF_SUITE"/include/libtest.shlib

#
# DESCRIPTION:
#	A resilver that starts while writes to a child fail counts no
#	checksum error on the healthy child.  A failed write in txg T
#	marks T in the DTL of the child before the resilver is set up in
#	the sync of T.  The resilver must not queue reads of blocks born
#	in T: a later sync pass of T rewrites them in place.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror of files.
#	2. Inject write errors on the second child, and write and sync in
#	   the background, so that each txg has failed writes.
#	3. Start one resilver with "zpool resilver" after a write error,
#	   wait for its setup event, then clear the injection.
#	4. Wait for the resilvers to end, and verify that the first child
#	   counted no checksum error, and that a scrub finds no error.
#

verify_runnable "global"

typeset pool=$TESTPOOL1
typeset dir=$TEST_BASE_DIR/zpool_resilver_failing
typeset vdev1=$dir/a vdev2=$dir/b
typeset -i writer=0

function get_vdev_error_count # <pool> <vdev> <column>
{
	zpool status -p "$1" | awk -v v="$2" -v c="$3" '
	    $1 == v { print $c; found = 1 }
	    END { if (!found) exit 1 }
	'
}

function get_resilver_setup_count
{
	zpool history -i "$pool" | awk '
	    /scan setup func=2/ { count++ }
	    END { print count + 0 }
	'
}

function cleanup
{
	(( writer > 0 )) && kill "$writer" > /dev/null 2>&1
	zinject -c all > /dev/null 2>&1
	poolexists "$pool" && destroy_pool "$pool"
	rm -rf "$dir"
}

log_onexit cleanup

log_assert "A resilver that starts while writes to a child fail counts" \
    "no checksum error on the healthy child."

log_must mkdir -p "$dir"
log_must truncate -s "$MINVDEVSIZE" "$vdev1" "$vdev2"
log_must zpool create -f -O mountpoint="$dir/fs" "$pool" mirror \
    "$vdev1" "$vdev2"

(
	typeset -i i=0
	while true; do
		dd if=/dev/urandom of="$dir/fs/f.$(( i++ % 8 ))" bs=128k \
		    count=4 2> /dev/null
		zpool sync "$pool"
	done
) &
writer=$!

typeset -i i
typeset write_errors_before write_errors
typeset setups_before setups
# A scan can start a follow-up resilver after it completes. Keep one
# explicit start so a later command cannot race that follow-up scan.
write_errors_before=$(get_vdev_error_count "$pool" "$vdev2" 4) ||
    log_fail "Unable to read write errors for $vdev2"
log_must zinject -d "$vdev2" -e io -T write "$pool"
for (( i = 0; i < 60; i++ )); do
	write_errors=$(get_vdev_error_count "$pool" "$vdev2" 4) ||
	    log_fail "Unable to read write errors for $vdev2"
	(( write_errors > write_errors_before )) && break
	sleep 1
done
(( write_errors > write_errors_before )) ||
    log_fail "No injected write error reached $vdev2"

setups_before=$(get_resilver_setup_count)
log_must zpool resilver "$pool"
for (( i = 0; i < 60; i++ )); do
	setups=$(get_resilver_setup_count)
	(( setups > setups_before )) && break
	sleep 1
done
(( setups > setups_before )) ||
    log_fail "No resilver started while writes failed"
sleep 3
log_must zinject -c all
log_must zpool wait -t resilver "$pool"

kill "$writer"
wait $writer
writer=0
log_must zpool sync "$pool"
log_must zpool wait -t resilver "$pool"

typeset cksum
cksum=$(get_vdev_error_count "$pool" "$vdev1" 5) ||
    log_fail "Unable to read checksum errors for $vdev1"
log_note "checksum errors on $vdev1: $cksum"
log_must test "$cksum" -eq 0
log_must zpool clear "$pool"
log_must zpool scrub -w "$pool"
log_must check_pool_status "$pool" errors "No known data errors"
cksum=$(get_vdev_error_count "$pool" "$vdev1" 5) ||
    log_fail "Unable to read checksum errors for $vdev1"
log_must test "$cksum" -eq 0

log_pass "A resilver that starts while writes to a child fail counts" \
    "no checksum error on the healthy child."
