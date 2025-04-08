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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/tests/functional/truncate/truncate.cfg
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# Ensure both truncate(2)/ftruncate(2) update target file mtime/ctime attributes
#
# STRATEGY:
# 1. Create a file
# 2. Truncate the file
# 3. Verify both mtime/ctime are updated
# 4. Rinse and repeat for both truncate(2) and ftruncate(2) with various sizes
#

verify_runnable "both"

function verify_truncate # <filename> <filesize> <option>
{
	typeset filename="$1"
	typeset -i size="$2"
	typeset option="$3"

	log_must mkfile $sizeavg $filename # always start with $sizeavg
	if is_freebsd; then
		typeset -i timestm="$(stat -f "%m" $filename)"
		typeset -i timestc="$(stat -f "%c" $filename)"
		log_must sleep 1
		log_must truncate_test -s $size $filename $option
		verify_eq $size "$(stat_size $filename)" "size"
		verify_ne $timestm "$(stat -f "%m" $filename)" "mtime"
		verify_ne $timestc "$(stat -f "%c" $filename)" "ctime"
	else
		typeset -i timestm="$(stat -c %Y $filename)"
		typeset -i timestc="$(stat -c %Z $filename)"
		log_must sleep 1
		log_must truncate_test -s $size $filename $option
		verify_eq $size "$(stat_size $filename)" "size"
		verify_ne $timestm "$(stat -c %Y $filename)" "mtime"
		verify_ne $timestc "$(stat -c %Z $filename)" "ctime"
	fi
	log_must rm -f $filename
}

function cleanup
{
	[[ -f $truncfile ]] && rm -f $truncfile
}

log_assert "Ensure both truncate(2)/ftruncate(2) update target file timestamps"
log_onexit cleanup

truncfile="$TESTDIR/truncate.$$"
sizemin="123"
sizeavg="$((256*1024))"
sizemax="$((1024*1024))"

# truncate(2)
verify_truncate $truncfile $sizemin ""
verify_truncate $truncfile $sizeavg ""
verify_truncate $truncfile $sizemax ""

# ftruncate(2)
verify_truncate $truncfile $sizemin "-f"
verify_truncate $truncfile $sizeavg "-f"
verify_truncate $truncfile $sizemax "-f"

log_pass "Successful truncation correctly update timestamps"
