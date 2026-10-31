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
#	A read of a range that a Direct I/O write replaces returns
#	the data from before the write or from after it.
#
# STRATEGY:
#	1. In a file system with recordsize=128k and direct=always,
#	   run the race of rangelock_regions.  Each write of a whole
#	   128K region goes to disk with Direct I/O.  Page-aligned
#	   reads use Direct I/O and the rest use the ARC.  The pool
#	   must count one Direct I/O write per write, and some but
#	   no more Direct I/O reads than aligned reads, since an
#	   aligned read of a block still cached is not counted.
#	2. In a file system with direct=standard, run it with
#	   writers that open the file O_DIRECT and readers that do
#	   not.  The pool must count one Direct I/O write per write
#	   and no Direct I/O reads.
#	3. Fail on any other read, on a count that differs, or if
#	   no read overlapped a write of its range.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/direct

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
	if tunable_exists DIO_ENABLED; then
		log_must restore_tunable DIO_ENABLED
	fi
}

#
# race direct flags: run the regions race in a new file system with
# the given direct property and helper flags, and check the pool's
# Direct I/O counts against the writes and aligned reads it made.
#
function race
{
	typeset direct=$1 flags=$2 mnt
	typeset -i wr0 rd0 wr rd dw dr writes reads

	log_must zfs create -o recordsize=128k -o direct=$direct $FS
	mnt=$(get_prop mountpoint $FS)
	log_must rangelock_race -p regions $mnt/file
	sync_pool $TESTPOOL
	wr0=$(kstat_pool $TESTPOOL iostats.direct_write_count)
	rd0=$(kstat_pool $TESTPOOL iostats.direct_read_count)
	rl_run 60 120 $flags regions $mnt/file
	wr=$(kstat_pool $TESTPOOL iostats.direct_write_count)
	rd=$(kstat_pool $TESTPOOL iostats.direct_read_count)
	((dw = wr - wr0, dr = rd - rd0))
	writes=$(rl_stat writes)
	reads=0
	[[ $direct == always ]] && reads=$(rl_stat aligned_reads)
	log_note "direct=$direct: $writes writes, $dw Direct I/O;" \
	    "$reads aligned reads, $dr Direct I/O"
	log_must test $writes -gt 0
	log_must test $dw -eq $writes
	log_must test $dr -le $reads
	if [[ $direct == always ]]; then
		log_must test $dr -gt 0
	fi
	destroy_dataset $FS -r
}

log_assert "Reads racing Direct I/O writes are not torn"
log_onexit cleanup

if tunable_exists DIO_ENABLED; then
	log_must save_tunable DIO_ENABLED
	log_must set_tunable32 DIO_ENABLED 1
fi

race always ""
race standard -D

log_pass "Reads racing Direct I/O writes are not torn"
