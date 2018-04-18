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
#	Verify FIEMAP can report all copies of data blocks.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fiemap/fiemap.kshlib

verify_runnable "both"

log_assert "FIEMAP reports all copies of data blocks"
log_onexit fiemap_cleanup

BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

for copies in 1 2 3; do
	log_must zfs set copies=$copies $TESTPOOL/$TESTFS

	# While the data is dirty only a single delalloc extent should be
	# reported.  Once it is written all of the requested copies should
	# be reported with overlapping logical extents.
	log_note "Multiple blocks"
	fiemap_write $BS 16
	fiemap_verify -c -D 0:$((BS*16)):1 -F "delalloc:all"
	fiemap_verify -s -c -D 0:$((BS*16)):$copies
	fiemap_remove

	# While the data is dirty only a single delalloc extent should be
	# reported.  Once the dirty data is converted in to holes it should
	# only be reported once.
	log_note "Large hole"
	fiemap_hole $BS 16
	fiemap_verify -c -D 0:$((BS*16)):1 -F "delalloc:all"
	fiemap_verify -s -H 0:$((BS*16)):1
	fiemap_verify -c -H 0:$((BS*16)):1
	fiemap_remove
done

log_pass "FIEMAP reports all copies of data blocks"
