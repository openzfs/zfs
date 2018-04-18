#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify FIEMAP unwritten extents are reported for pending frees.
#	Then after a sync the extents are fully described on disk.
#
# STRATEGY:
# 	1. Create an assortment of sparse file layouts.
#	2. Verify the expected delayed allocation extents are reported.
#	2. Verify the expected logical extents are reported after sync.
#	3. Repeat for the most common block sizes.
#
# Legend for ASCII block map:
# X - Completely filled block
# x - Partially filled block (may be compressed)
# . - Hole block
# o - Pending hole
# D - Full dirty block
# d - Partial dirty block
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fiemap/fiemap.kshlib

verify_runnable "both"

log_assert "FIEMAP reports pending free extents correctly"
log_onexit fiemap_cleanup

for recordsize in 4096 8192 16384 32768 65536 131072 ; do
	log_must zfs set recordsize=$recordsize $TESTPOOL/$TESTFS
	BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

	# Single block: o
	log_note "Single block"
	fiemap_write $BS 1
	fiemap_verify -s -D 0:$BS:1
	fiemap_free $BS 1
	fiemap_verify -H 0:$BS:1 -F "unwritten:0"
	fiemap_verify -h -H 0:$BS:1 -F "unwritten:1"
	fiemap_verify -s -H 0:$BS:1 -F "unwritten:0"
	fiemap_verify -h -H 0:$BS:1 -F "unwritten:1"
	fiemap_remove

	# Multiple blocks: oooooooo
	log_note "Multiple blocks"
	fiemap_write $BS 8
	fiemap_verify -s -D 0:$((BS*8)):1
	fiemap_free $BS 8
	fiemap_verify -H 0:$((BS*8)):1 -F "delalloc,unwritten:0"
	fiemap_verify -h -H 0:$((BS*8)):1 -F "delalloc,unwritten:1"
	fiemap_verify -s -H 0:$((BS*8)):1 -F "unwritten:0"
	fiemap_verify -h -H 0:$((BS*8)):1 -F "unwritten:1"
	fiemap_remove

	# Single partial block: o
	# Will be handled as a partial block write with compressed zeros
	log_note "Single partial block"
	fiemap_write $BS 1
	fiemap_verify -s -D 0:$BS:1
	fiemap_free $((BS/2)) 1 1
	fiemap_verify -D 0:$BS:1 -F "delalloc:1"
	fiemap_verify -s -D 0:$BS:1 -F "encoded:1"
	fiemap_remove

	# Multiple partial blocks: oooo
	log_note "Multiple partial blocks"
	fiemap_write $BS 4
	fiemap_verify -s -D 0:$((BS*4)):1
	fiemap_free $((BS/2)) 1 0
	fiemap_free $((BS/2)) 1 2
	fiemap_free $((BS/2)) 1 4
	fiemap_free $((BS/2)) 1 6
	fiemap_verify -D 0:$((BS*4)):1 -F "delalloc:all"
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_remove

	# Alternate dirty/free: DDDDDDD -> DoooooD -> DoDDDoD -> DoDoDoD
	log_note "Alternate overlapping pending dirty -> free -> dirty ->free"
	fiemap_write $BS 7 0
	fiemap_verify -D 0:$((BS*7)):1 -F "delalloc:all"
	fiemap_free $BS 5 1
	fiemap_verify -D 0:$BS:1 -H $BS:$((BS*5)):1 -D $((BS*6)):$BS:1 \
	    -F "delalloc,unwritten:0"
	fiemap_verify -h -D 0:$BS:1 -H $BS:$((BS*5)):1 -D $((BS*6)):$BS:1 \
	    -F "delalloc,unwritten:1"
	fiemap_write $BS 3 2
	fiemap_verify -D 0:$BS:1 -H $BS:$BS:1 -D $((BS*2)):$((BS*3)):1 \
	    -H $((BS*5)):$BS:1 -D $((BS*6)):$BS:1 -F "delalloc,unwritten:0"
	fiemap_verify -h -D 0:$BS:1 -H $BS:$BS:1 -D $((BS*2)):$((BS*3)):1 \
	    -H $((BS*5)):$BS:1 -D $((BS*6)):$BS:1 -F "delalloc,unwritten:2"
	fiemap_free $BS 1 3
	fiemap_verify -D 0:$BS:1 -H $BS:$BS:1 -D $((BS*2)):$BS:1 \
	    -H $((BS*3)):$BS:1 -D $((BS*4)):$BS:1 -H $((BS*5)):$BS:1 \
	    -D $((BS*6)):$BS:1 -F "delalloc,unwritten:0"
	fiemap_verify -h -D 0:$BS:1 -H $BS:$BS:1 -D $((BS*2)):$BS:1 \
	    -H $((BS*3)):$BS:1 -D $((BS*4)):$BS:1 -H $((BS*5)):$BS:1 \
	    -D $((BS*6)):$BS:1 -F "delalloc,unwritten:3"
	fiemap_verify -s -D 0:$BS:1 -H $BS:$BS:1 -D $((BS*2)):$BS:1 \
	    -H $((BS*3)):$BS:1 -D $((BS*4)):$BS:1 -H $((BS*5)):$BS:1 \
	    -D $((BS*6)):$BS:1 -F "delalloc:0"
	fiemap_remove
done

log_pass "FIEMAP reports pending free extents correctly"
