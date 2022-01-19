#!/bin/ksh -p
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
# Copyright (c) 2021 by Delphix. All rights reserved.
#

# DESCRIPTION
# Verify zfs destroy test for clones with livelists that contain
# dedup blocks. This test is a baseline regression test created
# to ensure that past bugs that we've encountered between dedup
# and the livelist logic don't resurface.

# STRATEGY
# 1. Create a clone from a test filesystem and enable dedup.
# 2. Write some data and create a livelist.
# 3. Copy the data within the clone to create dedup blocks.
# 4. Remove some of the dedup data to create multiple free
#    entries for the same block pointers.
# 5. Process all the livelist entries by destroying the clone.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

function cleanup
{
	log_must zfs destroy -Rf $TESTPOOL/$TESTFS1
	# Reset the minimum percent shared to 75
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED $ORIGINAL_MIN_SHARED
}

function test_dedup
{
	# Set a small percent shared threshold so the livelist is not disabled
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED 10
	clone_dataset $TESTFS1 snap $TESTCLONE

	# Enable dedup
	log_must zfs set dedup=on $TESTPOOL/$TESTCLONE

	# Create some data to be deduped
	log_must dd if=/dev/urandom of="/$TESTPOOL/$TESTCLONE/data" bs=512 count=10k

	# Create dedup blocks
	# Note: We sync before and after so all dedup blocks belong to the
	#       same TXG, otherwise they won't look identical to the livelist
	#       iterator due to their logical birth TXG being different.
	sync_pool $TESTPOOL
	log_must cp /$TESTPOOL/$TESTCLONE/data /$TESTPOOL/$TESTCLONE/data-dup-0
	log_must cp /$TESTPOOL/$TESTCLONE/data /$TESTPOOL/$TESTCLONE/data-dup-1
	log_must cp /$TESTPOOL/$TESTCLONE/data /$TESTPOOL/$TESTCLONE/data-dup-2
	log_must cp /$TESTPOOL/$TESTCLONE/data /$TESTPOOL/$TESTCLONE/data-dup-3
	sync_pool $TESTPOOL
	check_livelist_exists $TESTCLONE

	# Introduce "double frees"
	#   We want to introduce consecutive FREEs of the same block as this
	#   was what triggered past panics.
	# Note: Similarly to the previouys step we sync before and after our
	#       our deletions so all the entries end up in the same TXG.
	sync_pool $TESTPOOL
	log_must rm /$TESTPOOL/$TESTCLONE/data-dup-2
	log_must rm /$TESTPOOL/$TESTCLONE/data-dup-3
	sync_pool $TESTPOOL
	check_livelist_exists $TESTCLONE

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	check_livelist_gone
}

ORIGINAL_MIN_SHARED=$(get_tunable LIVELIST_MIN_PERCENT_SHARED)

log_onexit cleanup
# You might think that setting compression=off for $TESTFS1 would be
# sufficient. You would be mistaken.
# You need compression=off for whatever the parent of $TESTFS1 is,
# and $TESTFS1.
log_must zfs set compression=off $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS1
log_must mkfile 5m /$TESTPOOL/$TESTFS1/atestfile
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap
test_dedup

log_must zfs inherit compression $TESTPOOL

log_pass "Clone's livelist processes dedup blocks as expected."
