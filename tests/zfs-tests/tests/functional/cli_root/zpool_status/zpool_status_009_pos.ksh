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
# Copyright (c) 2026 by Michael Heller.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zpool status -L' does not truncate a long vdev path.
#
# STRATEGY:
# 1. Create a pool on a file vdev whose path is longer than the buffer the
#    resolved path used to be copied into.
# 2. Verify 'zpool status -L' reports the whole path.
# 3. Verify 'zpool status' without -L reports it too, which it always did.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -rf $longdir
}

log_assert "'zpool status -L' does not truncate a long vdev path"

log_onexit cleanup

#
# The resolved path has to exceed 63 bytes to reach the case this covers, so
# the name is padded out and the length checked rather than assumed.
#
longdir=$TESTDIR/$(printf 'd%.0s' {1..40})
longvdev=$longdir/$(printf 'v%.0s' {1..40})

typeset -i pathlen=${#longvdev}
if (( pathlen <= 63 )); then
	log_fail "vdev path is only $pathlen bytes, too short to truncate"
fi
log_note "vdev path is $pathlen bytes"

log_must mkdir -p $longdir
log_must truncate -s $MINVDEVSIZE $longvdev
log_must zpool create -f $TESTPOOL2 $longvdev

#
# -L resolves symlinks in the path.  A file vdev resolves to itself, so the
# whole path must come back unchanged.  Report whatever name was printed
# rather than only whether it matched, so a failure shows how far it was cut.
#
function vdev_name_from_status # args
{
	zpool status "$@" $TESTPOOL2 |
	    awk -v d="$TESTDIR" 'index($1, d) == 1 {print $1; exit}'
}

typeset name
name=$(vdev_name_from_status -L)
if [[ "$name" != "$longvdev" ]]; then
	log_fail "'zpool status -L' reported a ${#name} byte name," \
	    "expected $pathlen bytes: '$name'"
fi

name=$(vdev_name_from_status)
if [[ "$name" != "$longvdev" ]]; then
	log_fail "'zpool status' reported a ${#name} byte name," \
	    "expected $pathlen bytes: '$name'"
fi

log_pass "'zpool status -L' does not truncate a long vdev path"
