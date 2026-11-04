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
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
#	A Direct I/O write whose buffer changes in flight fails
#	checksum verify.  ZFS writes the range again from a private
#	copy of the buffer, still as Direct I/O.  After that, ZFS
#	copies every Direct I/O write on the same file handle before
#	issuing it, so those writes cannot fail verify.
#
# STRATEGY:
#	1. Turn on zfs_vdev_direct_write_verify and turn off
#	   compression, which copies the buffer before the checksum
#	   and so hides the change
#	2. Write a file with 128K O_DIRECT writes, one block each,
#	   while another thread changes the buffer
#	3. Every write must return its full length
#	4. If every write passes verify, repeat steps 2 and 3 from a
#	   new handle, up to ten tries in all
#	5. Verify must fail exactly once, because the handle copies
#	   after its first failure, and the pool must count every
#	   write and the retry as Direct I/O
#	6. Repeat on a new file with 32M writes of 256 blocks each.
#	   The copy goes out 16M at a time, so the redone write takes
#	   two passes.  Verify must fail 1 to 256 times, all in the
#	   first failing write, and the pool must count every block
#	   plus the 256 blocks of that write a second time
#	7. Read the files back and check the pool for errors
#

verify_runnable "global"

if is_freebsd; then
	log_unsupported "FreeBSD has stable pages for O_DIRECT writes"
fi

log_assert "A Direct I/O write that fails verify is copied and redone"

typeset -i NUMBLOCKS=300
typeset -i LNUM=4
typeset -i MAXTRIES=10
typeset -i BS LBS LBLOCKS
((BS = 128 * 1024))
((LBS = 32 * 1024 * 1024))
((LBLOCKS = LBS / BS))
typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset f="$mntpnt/dio-bounce.iso"
typeset lf="$mntpnt/dio-bounce-large.iso"
typeset -i writes fails

function cleanup
{
	rm -f $f $lf
	log_must zpool clear $TESTPOOL
	log_must zpool events -c
	log_must restore_tunable VDEV_DIRECT_WR_VERIFY
}
log_onexit cleanup

#
# Create file $1, then overwrite it with $3 O_DIRECT writes of $2 bytes
# from one handle while another thread changes the buffer.  Set writes
# and fails to the pool's new Direct I/O block writes and checksum
# verify failures.  Without -e the helper exits 2 on a failed or short
# write.
#
function bounce_pass # file bs count
{
	typeset -i nblks before fails_before after fails_after
	((nblks = $2 / BS * $3))

	log_must file_write -o create -f $1 -b $BS -c $nblks -w

	before=$(kstat_pool $TESTPOOL iostats.direct_write_count)
	fails_before=$(get_zpool_status_chksum_verify_failures \
	    $TESTPOOL "raidz")
	log_must manipulate_user_buffer -f $1 -n $3 -b $2 -w
	after=$(kstat_pool $TESTPOOL iostats.direct_write_count)
	fails_after=$(get_zpool_status_chksum_verify_failures \
	    $TESTPOOL "raidz")

	((writes = after - before))
	((fails = fails_after - fails_before))
	log_note "$1: Direct I/O writes $writes," \
	    "checksum verify failures $fails"

	log_must stride_dd -i $1 -o /dev/null -b $BS -c $nblks
}

#
# The helper starts its writer and mutator threads with no handshake,
# so a try can finish before the buffer changes under any write.  Run
# the pass again from a new handle until a write fails verify.
#
function bounce_until_fail # file bs count blocks-per-write
{
	typeset -i try
	for ((try = 1; try <= MAXTRIES; try++)); do
		bounce_pass $1 $2 $3
		((fails > 0)) && return
		if ((writes != $3 * $4)); then
			log_fail "$writes Direct I/O writes," \
			    "expected $(($3 * $4)) with none redone"
		fi
		log_note "Try $try had no verify failure"
	done
	log_fail "no write failed verify in $MAXTRIES tries"
}

log_must save_tunable VDEV_DIRECT_WR_VERIFY
log_must set_tunable32 VDEV_DIRECT_WR_VERIFY 1
log_must zfs set compression=off recordsize=128k $TESTPOOL/$TESTFS

bounce_until_fail $f $BS $NUMBLOCKS 1
if ((fails != 1)); then
	log_fail "$fails checksum verify failures, expected 1"
fi
if ((writes != NUMBLOCKS + fails)); then
	log_fail "$writes Direct I/O writes, expected $((NUMBLOCKS + fails))" \
	    "for $NUMBLOCKS writes plus $fails redone"
fi

#
# zfs_write() issues each 32M write as one chunk, so any of its blocks
# can fail verify and the whole write is redone.
#
bounce_until_fail $lf $LBS $LNUM $LBLOCKS
if ((fails < 1 || fails > LBLOCKS)); then
	log_fail "$fails checksum verify failures, expected 1 to $LBLOCKS"
fi
typeset -i lwrites
((lwrites = (LNUM + 1) * LBLOCKS))
if ((writes != lwrites)); then
	log_fail "$writes Direct I/O writes, expected $lwrites for" \
	    "$LNUM writes of $LBLOCKS blocks plus one redone"
fi

log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "A Direct I/O write that fails verify is copied and redone"
