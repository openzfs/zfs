#!/bin/ksh

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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

#
# Description:
# Verify that holes can be written and read back correctly in ZFS.
#
# Strategy:
# 1. Create a testfile with varying holes and data throughout the file.
# 2. Verify that each created file has the correct number of holes and
# data blocks as seen by both lseek and libzfs.
# 3. Do the same verification for a largefile.
# 4. Repeat for each recsize.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	rm -f $testfile
}

log_assert "Basic hole tests"
log_onexit cleanup

if ! mkholes -h 0:1024 $TESTDIR/setup_test; then
	log_unsupported "The kernel does not support FALLOC_FL_PUNCH_HOLE"
fi

testfile="$TESTDIR/testfile"
# testfile The file to examine.
# hole_blks The expected number of holes.
# data_blks The expected number of data blocks.
function verify_holes_and_data_blocks
{
	typeset testfile=$1
	typeset -i hole_blks=$2
	typeset -i data_blks=$3
	typeset -i failures=0

	found_hole_blks=$(getholes -h $testfile)
	found_data_blks=$(getholes -d $testfile)
	if [[ $found_hole_blks -ne $hole_blks ]] then;
		log_note "Found $found_hole_blks, not $hole_blks hole blocks."
		((failures++))
	fi

	if [[ $found_data_blks -ne $data_blks ]] then;
		log_note "Found $found_data_blks, not $data_blks data blocks."
		((failures++))
	fi

	[[ $failures -eq 0 ]] || log_fail "Wrong number of data/hole blocks."
}

for bs in 1024 4096 16384 65536 262144; do
	log_must zfs set recsize=$bs $TESTPOOL/$TESTFS

	#
	# Create combinations of holes and data to verify holes ending files
	# and the like. (hhh, hhd, hdh...)
	#
	log_must mkholes -h 0:$((bs * 6)) $testfile
	verify_holes_and_data_blocks $testfile 6 0
	log_must rm $testfile

	log_must mkholes -h 0:$((bs * 4)) -d $((bs * 4)):$((bs * 2)) $testfile
	verify_holes_and_data_blocks $testfile 4 2
	log_must rm $testfile

	log_must mkholes -h 0:$((bs * 2)) -d $((bs * 2)):$((bs * 2)) \
	    -h $((bs * 4)):$((bs * 2)) $testfile
	verify_holes_and_data_blocks $testfile 4 2
	log_must rm $testfile

	log_must mkholes -h 0:$((bs * 2)) -d $((bs * 2)):$((bs * 4)) $testfile
	verify_holes_and_data_blocks $testfile 2 4
	log_must rm $testfile

	log_must mkholes -d 0:$((bs * 2)) -h $((bs * 2)):$((bs * 4)) $testfile
	verify_holes_and_data_blocks $testfile 4 2
	log_must rm $testfile

	log_must mkholes -d 0:$((bs * 2)) -h $((bs * 2)):$((bs * 2)) \
	    -d $((bs * 4)):$((bs * 2)) $testfile
	verify_holes_and_data_blocks $testfile 2 4
	log_must rm $testfile

	log_must mkholes -d 0:$((bs * 4)) -h $((bs * 4)):$((bs * 2)) $testfile
	verify_holes_and_data_blocks $testfile 2 4
	log_must rm $testfile

	log_must mkholes -d 0:$((bs * 6)) $testfile
	verify_holes_and_data_blocks $testfile 0 6
	log_must rm $testfile

	# Verify holes are correctly seen past the large file limit.
	len=$((1024**3 * 5))
	nblks=$((len / bs))
	log_must mkholes -h 0:$len -d $len:$bs $testfile
	verify_holes_and_data_blocks $testfile $nblks 1
	log_must rm $testfile
done

log_pass "Basic hole tests pass."
