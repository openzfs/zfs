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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018, 2020 by Delphix. All rights reserved.
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
# 4. Multiple clones with populated livelists
#	- same as 2. but with multiple clones
# 5. Clone of clone with populated livelists with promote

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1 -R
	# reset the livelist sublist size to its original value
	set_tunable64 LIVELIST_MAX_ENTRIES $ORIGINAL_MAX
	log_must zfs inherit compression $TESTPOOL
}

function clone_write_file
{
	log_must mkfile 1m /$TESTPOOL/$1/$2
	sync_pool $TESTPOOL
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

function test_clone_clone_promote
{
	log_must zfs create $TESTPOOL/fs
	log_must dd if=/dev/zero of=/$TESTPOOL/fs/file bs=128k count=100
	log_must zfs snapshot $TESTPOOL/fs@snap
	log_must zfs clone $TESTPOOL/fs@snap $TESTPOOL/clone
	log_must dd if=/dev/zero of=/$TESTPOOL/clone/clonefile bs=128k count=10
	log_must zfs snapshot $TESTPOOL/clone@csnap
	log_must zfs clone $TESTPOOL/clone@csnap $TESTPOOL/cloneclone

	check_livelist_exists clone
	check_livelist_exists cloneclone

	# Promote should remove both clones' livelists
	log_must zfs promote $TESTPOOL/cloneclone
	check_livelist_gone

	# This destroy should not use a livelist
	log_must zfs destroy $TESTPOOL/clone
	log_must zdb -bcc $TESTPOOL
}

ORIGINAL_MAX=$(get_tunable LIVELIST_MAX_ENTRIES)

log_onexit cleanup
# You might think that setting compression=off for $TESTFS1 would be
# sufficient. You would be mistaken.
# You need compression=off for whatever the parent of $TESTFS1 is,
# and $TESTFS1.
log_must zfs set compression=off $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS1
log_must mkfile 20m /$TESTPOOL/$TESTFS1/atestfile
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap

# set a small livelist entry size to more easily test multiple entry livelists
set_tunable64 LIVELIST_MAX_ENTRIES 20

test_one_empty
test_one
test_multiple_empty
test_multiple
test_promote
test_clone_clone_promote

log_pass "Clone with the livelist feature enabled could be destroyed," \
	"also could be promoted and destroyed as expected."
