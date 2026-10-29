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

. $STF_SUITE/tests/functional/rangelock/rangelock.kshlib

#
# DESCRIPTION:
#	A read of a range that FIDEDUPERANGE replaces returns the
#	same data.  A dedupe within one file, which holds a reader
#	and a writer range lock on the file at once, does not hang.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, write three files
#	   of 24 regions of 128K with the same data in separately
#	   written blocks, and sync the pool.
#	2. One thread dedupes the 8 middle regions of the first file
#	   against the other two files in turn, so each dedupe
#	   replaces the blocks.  Another dedupes them against the
#	   regions below and above them in the same file, which
#	   takes the two locks in both orders.  Four threads read
#	   the middle regions.
#	3. Each read must return the data unchanged.  Each dedupe
#	   must report the ranges the same and all bytes deduped.
#	4. Fail on any other read, on a dedupe stuck past the hang
#	   limit, or if no read overlapped a dedupe of its range.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "FIDEDUPERANGE is Linux only"
fi

FS=$TESTPOOL/$TESTFS/dedupe

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
	if tunable_exists BCLONE_ENABLED; then
		log_must restore_tunable BCLONE_ENABLED
	fi
}

log_assert "Reads racing dedupes of the same range see the same data"
log_onexit cleanup

if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

log_must zfs create -o recordsize=128k $FS
MNT=$(get_prop mountpoint $FS)
log_must rangelock_race -p dedupe $MNT/file
sync_pool $TESTPOOL
rl_run 60 120 dedupe $MNT/file

log_pass "Reads racing dedupes of the same range see the same data"
