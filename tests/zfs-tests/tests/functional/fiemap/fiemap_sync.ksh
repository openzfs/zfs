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
#	Verify FIEMAP extents on disk.  Sync is forced prior to
#	reading extents so there will be no dirty extents.
#
# STRATEGY:
# 	1. Create an assortment of sparse file layouts.
#	2. Verify the expected logical extents are reported.
#	3. Repeat for the most common block sizes.
#
# Legend for ASCII block map:
# X - Completely filled block
# x - Partially filled block (may be compressed)
# . - Hole block
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fiemap/fiemap.kshlib

verify_runnable "both"

log_assert "FIEMAP reports synced extents"
log_onexit fiemap_cleanup

for recordsize in 4096 8192 16384 32768 65536 131072 ; do
	log_must zfs set recordsize=$recordsize $TESTPOOL/$TESTFS
	BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

	# Single block: X
	log_note "Single block"
	fiemap_write $BS 1
	fiemap_verify -s -D 0:$BS:1
	fiemap_remove

	# Multiple blocks: XXXXXXXX
	log_note "Multiple blocks"
	fiemap_write $BS 8
	fiemap_verify -s -D 0:$((BS*8)):1
	fiemap_remove

	# Single partial block: x
	log_note "Single partial block"
	fiemap_write $((BS/2+1)) 1
	fiemap_verify -s -D 0:$((BS/2+1)):1
	fiemap_remove

	# Multiple partial blocks: xxxx
	log_note "Partial blocks"
	fiemap_write $((BS/2)) 1 0
	fiemap_write $((BS/2)) 1 2
	fiemap_write $((BS/2)) 1 4
	fiemap_write $((BS/2)) 1 6
	fiemap_verify -s -D 0:$((BS/2*7)):1
	fiemap_remove

	# Punch a full block hole: XX.X
	log_note "Punch a single full block hole"
	fiemap_write $BS 4
	fiemap_hole $BS 1 2
	fiemap_verify -s -D 0:$((BS*2)):1 -H $((BS*2)):$BS:1 -D $((BS*3)):$BS:1
	fiemap_remove

	# Punch multiple holes of different sizes: .X..XX...
	log_note "Punch multiple different sized holes"
	fiemap_write $BS 9
	fiemap_hole $BS 1 0
	fiemap_hole $BS 2 2
	fiemap_hole $BS 3 6
	fiemap_verify -s -H 0:$BS:1 -D $BS:$BS:1 -H $((BS*2)):$((BS*2)):1 \
	    -D $((BS*4)):$((BS*2)):1 -H $((BS*6)):$((BS*3)):1
	fiemap_remove

	# Punch a sub-block hole: XxxX
	log_note "Punch a single sub-block hole"
	fiemap_write $BS 4
	fiemap_hole $((BS/2)) 2 3
	fiemap_verify -s -D 0:$((BS*4)):1
	fiemap_remove

	# Single block hole: .
	log_note "Single block hole"
	fiemap_hole $BS 1
	fiemap_verify -s -H 0:$BS:1
	fiemap_remove

	# Multiple block holes: ........
	log_note "Multiple block holes"
	fiemap_hole $BS 8
	fiemap_verify -s -H 0:$((BS*8)):1
	fiemap_remove

	# Write blocks in to holes: ...X..XX
	log_note "Write blocks in to holes"
	fiemap_hole $BS 8
	fiemap_write $BS 1 3
	fiemap_write $BS 2 6
	fiemap_verify -s -H 0:$((BS*3)):1 -D $((BS*3)):$BS:1 \
	    -H $((BS*4)):$((BS*2)):1 -D $((BS*6)):$((BS*2)):1
	fiemap_remove
done

log_pass "FIEMAP extents on disk"
