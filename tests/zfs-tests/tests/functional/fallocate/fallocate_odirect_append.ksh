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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A fallocate that extends a file keeps a concurrent O_DIRECT
#	append that grew the file further.
#
# STRATEGY:
#	1. Write one block to the file
#	2. Start an O_DIRECT append at EOF and extend the file with
#	   fallocate at the same time
#	3. Verify the file is at least as large as the append made it
#	4. Repeat ROUNDS times to hit the race
#

verify_runnable "global"

log_assert "fallocate does not discard a racing O_DIRECT append"

typeset -i ROUNDS=100
typeset -i BS=131072

function cleanup
{
	rm -f $TESTDIR/racefile
}
log_onexit cleanup

typeset f=$TESTDIR/racefile
typeset -i lost=0
typeset -i i=0

while ((i < ROUNDS)); do
	((i = i + 1))
	rm -f $f
	log_must dd if=/dev/zero of=$f bs=$BS count=1 conv=fsync
	typeset -i sz=$(stat_size $f)

	dd if=/dev/urandom of=$f bs=$BS count=1 seek=1 oflag=direct \
	    conv=notrunc >/dev/null 2>&1 &
	typeset -i ap=$!
	log_must fallocate -l $((sz + 4096)) $f
	log_must wait $ap

	typeset -i now=$(stat_size $f)
	if ((now < sz + BS)); then
		log_note "round $i: size $now, expected $((sz + BS))"
		((lost = lost + 1))
	fi
done

if ((lost > 0)); then
	log_fail "$lost of $ROUNDS rounds lost the appended block"
fi

log_pass "fallocate does not discard a racing O_DIRECT append"
