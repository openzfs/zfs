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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	A pinned file that code without this feature grew to one odd-sized
#	block can grow again.
#
# STRATEGY:
#	1. Receive a stream with such a file: pinned to 8K, then written to
#	   9000 bytes by older code, so it has one 9216-byte block.
#	2. Check that the file isn't reported as pinned, that clearing it is
#	   accepted (there is nothing to clear), and that it can be appended
#	   to, grows to recordsize blocks and reads back.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset dst=$TESTPOOL/$TESTFS/odd
typeset stream=$STF_SUITE/tests/functional/fileblocksize/oddblk.zsend.bz2

function cleanup
{
	datasetexists $dst && destroy_dataset $dst -r
	rm -f $TEST_BASE_DIR/odd.copy $TEST_BASE_DIR/odd.new
}

log_onexit cleanup
log_assert "a pinned file grown by older code can grow again"

typeset copy=$TEST_BASE_DIR/odd.copy

for clear in no yes; do
	log_must eval "bzcat $stream | zfs receive $dst"
	log_must zfs set readonly=off $dst
	typeset f=$(get_prop mountpoint $dst)/odd

	verify_ext $f none 0
	[[ $clear == yes ]] && log_must extsize -c $f
	log_must cp $f $copy

	log_must dd if=/dev/urandom of=$TEST_BASE_DIR/odd.new bs=20000 \
	    count=1
	log_must eval "cat $TEST_BASE_DIR/odd.new >>$copy"
	log_must eval "cat $TEST_BASE_DIR/odd.new >>$f"
	write_mib $TEST_BASE_DIR/odd.new
	log_must eval "cat $TEST_BASE_DIR/odd.new >>$copy"
	log_must eval "cat $TEST_BASE_DIR/odd.new >>$f"
	log_must rm $TEST_BASE_DIR/odd.new
	log_must zpool sync $TESTPOOL
	log_must zfs unmount $dst
	log_must zfs mount $dst
	log_must cmp $copy $f
	verify_blksz $f 131072
	log_must destroy_dataset $dst -r
done

log_pass "a pinned file grown by older code can grow again"
