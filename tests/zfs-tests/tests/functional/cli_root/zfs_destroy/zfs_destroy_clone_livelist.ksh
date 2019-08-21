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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

# DESCRIPTION
# Verify zfs destroy test for clones with the livelist feature
# enabled.

# STRATEGY
# 1. One clone with an empty livelist
#	- create the clone, check that livelist exists
#	- delete the clone, check that livelist is eventually
#	  destroyed
# 2. One clone with populated livelist
#	- create the clone, check that livelist exists
#	- write multiple files to the clone
#	- delete the clone, check that livelist is eventually
#	  destroyed
# 3. Multiple clones with empty livelists
#	- same as 1. but with multiple clones
# 4. Multuple clones with populated livelists
#	- same as 2. but with multiple clones

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && zfs destroy -R $TESTPOOL/$TESTFS1
	# reset the livelist sublist size to its original value
	set_tunable64 zfs_livelist_max_entries $ORIGINAL_MAX
}

function clone_write_file
{
	log_must mkfile 1m /$TESTPOOL/$1/$2
	log_must zpool sync $TESTPOOL
}

function test_one_empty
{
	clone_dataset $TESTFS1 snap $TESTCLONE

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	check_livelist_gone
}

function test_one
{
	clone_dataset $TESTFS1 snap $TESTCLONE

	clone_write_file $TESTCLONE $TESTFILE0
	clone_write_file $TESTCLONE $TESTFILE1
	clone_write_file $TESTCLONE $TESTFILE2
	log_must rm /$TESTPOOL/$TESTCLONE/$TESTFILE0
	log_must rm /$TESTPOOL/$TESTCLONE/$TESTFILE2
	check_livelist_exists $TESTCLONE

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	check_livelist_gone
}

function test_multiple_empty
{
	clone_dataset $TESTFS1 snap $TESTCLONE
	clone_dataset $TESTFS1 snap $TESTCLONE1
	clone_dataset $TESTFS1 snap $TESTCLONE2

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	log_must zfs destroy $TESTPOOL/$TESTCLONE1
	log_must zfs destroy $TESTPOOL/$TESTCLONE2
	check_livelist_gone
}

function test_multiple
{
	clone_dataset $TESTFS1 snap $TESTCLONE
	clone_dataset $TESTFS1 snap $TESTCLONE1
	clone_dataset $TESTFS1 snap $TESTCLONE2

	clone_write_file $TESTCLONE $TESTFILE0

	clone_write_file $TESTCLONE1 $TESTFILE0
	clone_write_file $TESTCLONE1 $TESTFILE1
	clone_write_file $TESTCLONE1 $TESTFILE2

	clone_write_file $TESTCLONE2 $TESTFILE0
	log_must rm /$TESTPOOL/$TESTCLONE2/$TESTFILE0
	clone_write_file $TESTCLONE2 $TESTFILE1
	log_must rm /$TESTPOOL/$TESTCLONE2/$TESTFILE1

	check_livelist_exists $TESTCLONE
	check_livelist_exists $TESTCLONE1
	check_livelist_exists $TESTCLONE2

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	log_must zfs destroy $TESTPOOL/$TESTCLONE1
	log_must zfs destroy $TESTPOOL/$TESTCLONE2
	check_livelist_gone
}

function test_promote
{
	clone_dataset $TESTFS1 snap $TESTCLONE

	log_must zfs promote $TESTPOOL/$TESTCLONE
	check_livelist_gone
	log_must zfs destroy -R $TESTPOOL/$TESTCLONE
}

ORIGINAL_MAX=$(get_tunable zfs_livelist_max_entries)

log_onexit cleanup
log_must zfs create $TESTPOOL/$TESTFS1
log_must mkfile 20m /$TESTPOOL/$TESTFS1/atestfile
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap

# set a small livelist entry size to more easily test multiple entry livelists
set_tunable64 zfs_livelist_max_entries 20

test_one_empty
test_one
test_multiple_empty
test_multiple
test_promote

log_pass "Clone with the livelist feature enabled could be destroyed," \
	"also could be promoted and destroyed as expected."
