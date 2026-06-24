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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# A fallocate(2) that extends a file past EOF must update mtime/ctime; a
# FALLOC_FL_KEEP_SIZE preallocation must not change the size or timestamps.
#
# STRATEGY:
# 1. Create a small file and record its mtime/ctime
# 2. fallocate -l to a larger size (extend) and verify the size grew and both
#    mtime and ctime advanced
# 3. fallocate --keep-size to a larger length and verify the size and both
#    timestamps are unchanged
#

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
STARTSIZE=$((128 * 1024))

function cleanup
{
	[[ -f $FILE ]] && rm -f $FILE
}

# extend past EOF: size must grow and mtime/ctime must advance
function verify_extend # <newsize>
{
	typeset -i size="$1"

	log_must mkfile $STARTSIZE $FILE
	typeset -i tm="$(stat -c %Y $FILE)"
	typeset -i tc="$(stat -c %Z $FILE)"
	log_must sleep 1
	log_must fallocate -l $size $FILE
	verify_eq $size "$(stat_size $FILE)" "size"
	verify_ne $tm "$(stat -c %Y $FILE)" "mtime"
	verify_ne $tc "$(stat -c %Z $FILE)" "ctime"
	log_must rm -f $FILE
}

log_assert "Ensure fallocate extend updates mtime/ctime and --keep-size does not"
log_onexit cleanup

verify_extend $((256 * 1024))
verify_extend $((1024 * 1024))

# --keep-size preallocation must not change size or timestamps
log_must mkfile $STARTSIZE $FILE
typeset -i tm="$(stat -c %Y $FILE)"
typeset -i tc="$(stat -c %Z $FILE)"
log_must sleep 1
log_must fallocate --keep-size -l $((1024 * 1024)) $FILE
verify_eq $STARTSIZE "$(stat_size $FILE)" "size"
verify_eq $tm "$(stat -c %Y $FILE)" "mtime"
verify_eq $tc "$(stat -c %Z $FILE)" "ctime"
log_must rm -f $FILE

log_pass "fallocate extend correctly updates timestamps"
