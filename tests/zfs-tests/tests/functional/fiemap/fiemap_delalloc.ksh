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
#	Verify FIEMAP delayed allocation extents.  After dirtying
#	the block verify it is reported as a delayed allocation.
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

log_assert "FIEMAP reports delayed allocation extents"
log_onexit fiemap_cleanup

for recordsize in 4096 8192 16384 32768 65536 131072 ; do
	log_must zfs set recordsize=$recordsize $TESTPOOL/$TESTFS
	BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

	# Single block: X
	log_note "Single block"
	fiemap_write $BS 1
	fiemap_verify -D 0:$BS:1 -F "delalloc:1"
	fiemap_verify -s -D 0:$BS:1 -F "delalloc:0"
	fiemap_remove

	# Multiple blocks: XXXXXXXX
	log_note "Multiple blocks"
	fiemap_write $BS 8
	fiemap_verify -D 0:$((BS*8)):1 -F "delalloc:all"
	fiemap_verify -s -D 0:$((BS*8)):1 -F "delalloc:0"
	fiemap_remove

	# Single partial block: x
	log_note "Single partial block"
	fiemap_write $((BS/2+1)) 1
	fiemap_verify -D 0:$((BS/2+1)):1 -F "delalloc:all"
	fiemap_verify -s -D 0:$((BS/2+1)):1 -F "delalloc:0"
	fiemap_remove

	# Multiple partial blocks: xxxx
	log_note "Partial blocks"
	fiemap_write $((BS/2)) 1 0
	fiemap_write $((BS/2)) 1 2
	fiemap_write $((BS/2)) 1 4
	fiemap_write $((BS/2)) 1 6
	fiemap_verify -D 0:$((BS/2*7)):1 -F "delalloc:all"
	fiemap_verify -s -D 0:$((BS/2*7)):1 -F "delalloc:0"
	fiemap_remove

	# Overwrite a full block: XXDX
	log_note "Overwrite a full block"
	fiemap_write $BS 4
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_write $BS 1 2
	fiemap_verify -D 0:$((BS*4)):1 -F "delalloc:1"
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_remove

	# Overwrite a partial block: XXdX
	log_note "Overwrite a partial block"
	fiemap_write $BS 4
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_write $((BS/2)) 1 4
	fiemap_verify -D 0:$((BS*4)):1 -F "delalloc:1"
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_remove

	# Overwrite two partial blocks: XddX
	log_note "Overwrite two partial blocks"
	fiemap_write $BS 4
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_write $((BS/2)) 2 3
	fiemap_verify -D 0:$((BS*4)):1 -F "delalloc:1"
	fiemap_verify -s -D 0:$((BS*4)):1 -F "delalloc:0"
	fiemap_remove

	# Overwrite multiple entire extents: XDDDDDX
	log_note "Overwrite multiple entire extents"
	fiemap_write_compressible $BS 6
	fiemap_verify -s -D 0:$((BS*6)):1 -F "delalloc:0"
	fiemap_write_compressible $BS 1 1
	fiemap_write_compressible $BS 1 3
	fiemap_write_compressible $BS 1 5
	fiemap_verify -s -D 0:$((BS*6)):1 -F "delalloc:0"
	fiemap_verify -s -D 0:$((BS*6)):1 -F "encoded:6"
	fiemap_write $BS 4 1
	fiemap_verify -D 0:$((BS*6)):1 -F "delalloc:1"
	fiemap_verify -D 0:$((BS*6)):1 -F "encoded:2"
	fiemap_verify -s -D 0:$((BS*6)):1 -F "delalloc:0"
	fiemap_verify -s -D 0:$((BS*6)):1 -F "encoded:2"
	fiemap_remove

	# Overwrite an existing hole: .DDD....
	log_note "Overwrite an existing hole"
	fiemap_hole $BS 8
	fiemap_verify -s -h -H 0:$((BS*8)):1 -F "unwritten:1"
	fiemap_write $BS 3 1
	fiemap_verify -h -H 0:$((BS*1)):1 -D $((BS)):$((BS*3)):1 \
	    -H $((BS*4)):$((BS*4)):1 -F "unwritten:2"
	fiemap_verify -H 0:$((BS*1)):1 -D $((BS)):$((BS*3)):1 \
	    -H $((BS*4)):$((BS*4)):1 -F "delalloc:1"
	fiemap_verify -h -s -H 0:$((BS*1)):1 -D $((BS)):$((BS*3)):1 \
	    -H $((BS*4)):$((BS*4)):1 -F "unwritten:2"
	fiemap_verify -s -H 0:$((BS*1)):1 -D $((BS)):$((BS*3)):1 \
	    -H $((BS*4)):$((BS*4)):1 -F "delalloc:0"
	fiemap_remove

	# Overwrite a pending hole: .XXX.... -> .ooo.... -> .oDDD...
	log_note "Overwrite a pending hole"
	fiemap_hole $BS 8 0
	fiemap_write $BS 3 1
	fiemap_verify -s -H 0:$((BS*1)):1 -D $((BS)):$((BS*3)):1 \
	    -H $((BS*4)):$((BS*4)):1 -F "delalloc:0"
	fiemap_hole $BS 3 1
	fiemap_write $BS 3 2
	# The zero's written by are considered dirty data until synced
	fiemap_verify -H 0:$((BS*1)):1 -D $((BS)):$((BS*4)):1 \
	    -H $((BS*5)):$((BS*3)):1 -F "delalloc:1"
	# Once synced the first dirty block is converted to a hole.
	fiemap_verify -s -H 0:$((BS*2)):1 -D $((BS*2)):$((BS*3)):1 \
	    -H $((BS*5)):$((BS*3)):1 -F "delalloc:0"
	fiemap_remove
done

log_pass "FIEMAP reports delayed allocation extents"
