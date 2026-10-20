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
#	An extending fallocate, a keep-size punch at EOF and a
#	keep-size zero-range at EOF each keep a concurrent O_DIRECT
#	append that grew the file.
#
# STRATEGY:
#	1. Set recordsize=128k and direct=standard so the append
#	   takes the Direct I/O path
#	2. For each of the three fallocate modes, run
#	   dio_append_fallocate for ROUNDS rounds
#	3. Each round writes one block, then appends a block with
#	   O_DIRECT while another thread calls fallocate
#	4. Log each mode's count of rounds that left the file smaller
#	   than the append made it
#	5. Skip a mode the kernel does not support
#	6. Fail if any mode lost a round
#

verify_runnable "global"

log_assert "fallocate does not discard a racing O_DIRECT append"

typeset -i ROUNDS=100

function cleanup
{
	rm -f $TESTDIR/racefile
	log_must zfs inherit recordsize $TESTPOOL/$TESTFS
	log_must zfs inherit direct $TESTPOOL/$TESTFS
}
log_onexit cleanup

typeset f=$TESTDIR/racefile
typeset mode out failed=""
typeset -i rc

log_must zfs set recordsize=128k direct=standard $TESTPOOL/$TESTFS

for mode in extend punch zero; do
	out=$(dio_append_fallocate $mode $f $ROUNDS 2>&1)
	rc=$?
	case $rc in
	0)
		log_note "$out"
		;;
	1)
		log_note "$out"
		failed="$failed $mode"
		;;
	4)
		log_note "$mode is not supported: $out"
		;;
	*)
		log_fail "$mode: dio_append_fallocate exited $rc:" \
		    "$out"
		;;
	esac
done

if [[ -n $failed ]]; then
	log_fail "fallocate discarded a racing O_DIRECT append" \
	    "in:$failed"
fi

log_pass "fallocate does not discard a racing O_DIRECT append"
