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
# Copyright (c) 2026 by Benjamin Hodgens <ben@hodgens.net>
#

# DESCRIPTION:
# Pool compatibility gating: a pool whose compatibility set excludes
# org.openzfs:events refuses events=on, naming the feature; a pool with
# compatibility=off accepts it.
#
# STRATEGY:
# 1. Build a compatibility file from openzfs-2.4 (no events token) plus
#    the required trailing newline.
# 2. Create a pool with -o compatibility=<file> on a file vdev; verify
#    zfs set events=on fails naming org.openzfs:events and the feature
#    stays disabled.
# 3. Create a second pool with compatibility=off; verify events=on
#    succeeds.
# 4. Destroy both extra pools on every path.

. $STF_SUITE/include/libtest.shlib

typeset -r COMPAT_SRC="$ZPOOL_COMPAT_DIR/openzfs-2.4"

function cleanup_compat_pool # <pool>
{
	typeset pool="$1"

	if poolexists "$pool"; then
		log_must zpool destroy -f "$pool"
	fi
	[[ -e "$pool.img" ]] && log_must rm -f "$pool.img"
}

function cleanup
{
	cleanup_compat_pool "fe-compat-on"
	cleanup_compat_pool "fe-compat-off"
	[[ -d "$TMPDIR/file_events_compat.$$" ]] && \
	    log_must rm -rf "$TMPDIR/file_events_compat.$$"
}

log_assert "compatibility set without org.openzfs:events refuses events=on"

typeset wd="$TMPDIR/file_events_compat.$$"
log_must mkdir -p "$wd"

# openzfs-2.4 has no events token; the trailing newline is required
# because the loader NULs the last byte of the file.
typeset compatfile="$wd/no-events"
log_must cp "$COMPAT_SRC" "$compatfile"
log_must printf '\n' >>"$compatfile"

typeset -r img1="$wd/compat.img"
typeset -r img2="$wd/compat-off.img"
log_must truncate -s 256M "$img1"
log_must truncate -s 256M "$img2"

typeset -r pool1="fe-compat-on"
typeset -r pool2="fe-compat-off"
typeset -r ds1="$pool1/fs"
typeset -r ds2="$pool2/fs"

function verify_refusal
{
	typeset err="$wd/set1.err"
	typeset out="$wd/set1.out"
	zfs set events=on "$ds1" >"$out" 2>"$err"
	typeset -i rc=$?
	[[ $rc -ne 0 ]] ||
	    log_fail "compatibility-file pool accepted events=on"
	grep -q "org.openzfs:events" "$err" ||
	    log_fail "refusal does not name org.openzfs:events: $(cat "$err")"
	typeset val=$(zpool get -H -o value feature@events "$pool1")
	[[ "$val" == "disabled" ]] ||
	    log_fail "feature@events=$val after refusal, expected disabled"
}

function verify_accept
{
	log_must zfs set events=on "$ds2"
	typeset val=$(zfs get -H -o value events "$ds2")
	[[ "$val" == "on" ]] ||
	    log_fail "compatibility=off pool: events=$val, expected on"
}

log_must zpool create -f -o compatibility="$compatfile" "$pool1" "$img1"
log_must zfs create "$ds1"
verify_refusal
log_must zfs get -H -o value compatibility "$pool1" >"$wd/compat.val"
grep -q "no-events" "$wd/compat.val" ||
    log_fail "compatibility readback: $(cat "$wd/compat.val")"

log_must zpool create -f -o compatibility=off "$pool2" "$img2"
log_must zfs create "$ds2"
verify_accept

log_must zpool destroy -f "$pool1"
log_must zpool destroy -f "$pool2"
log_must rm -rf "$wd"

log_pass "compatibility set without org.openzfs:events refuses events=on"
