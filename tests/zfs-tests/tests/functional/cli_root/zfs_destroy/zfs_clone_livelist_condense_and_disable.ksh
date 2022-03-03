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
# 1. Clone where livelist is condensed
#	- create clone, write several files, delete those files
#	- check that the number of livelist entries decreases
#	  after the delete
# 2. Clone where livelist is deactivated
#	- create clone, write files. Delete those files and the
#	  file in the filesystem when the snapshot was created
#	  so the clone and snapshot no longer share data
#	- check that the livelist is destroyed

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

function cleanup
{
	log_must zfs destroy -Rf $TESTPOOL/$TESTFS1
	# reset the livelist sublist size to the original value
	set_tunable64 LIVELIST_MAX_ENTRIES $ORIGINAL_MAX
	# reset the minimum percent shared to 75
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED $ORIGINAL_MIN
	log_must zfs inherit compression $TESTPOOL
}

function check_ll_len
{
    string="$(zdb -vvvvv $TESTPOOL | grep "Livelist")"
    substring="$1"
    msg=$2
    if test "${string#*$substring}" != "$string"; then
        return 0    # $substring is in $string
    else
	log_note $string
        log_fail "$msg" # $substring is not in $string
    fi
}

function test_condense
{
	# set the max livelist entries to a small value to more easily
	# trigger a condense
	set_tunable64 LIVELIST_MAX_ENTRIES 20
	# set a small percent shared threshold so the livelist is not disabled
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED 10
	clone_dataset $TESTFS1 snap $TESTCLONE

	# sync between each write to make sure a new entry is created
	for i in {0..4}; do
	    log_must mkfile 5m /$TESTPOOL/$TESTCLONE/testfile$i
	    sync_pool $TESTPOOL
	done

	check_ll_len "5 entries" "Unexpected livelist size"

	# sync between each write to allow for a condense of the previous entry
	for i in {0..4}; do
	    log_must mkfile 5m /$TESTPOOL/$TESTCLONE/testfile$i
	    sync_pool $TESTPOOL
	done

	check_ll_len "6 entries" "Condense did not occur"

	log_must zfs destroy $TESTPOOL/$TESTCLONE
	check_livelist_gone
}

function test_deactivated
{
	# Threshold set to 50 percent
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED 50
	clone_dataset $TESTFS1 snap $TESTCLONE

	log_must mkfile 5m /$TESTPOOL/$TESTCLONE/$TESTFILE0
	log_must mkfile 5m /$TESTPOOL/$TESTCLONE/$TESTFILE1
	sync_pool $TESTPOOL
	# snapshot and clone share 'atestfile', 33 percent
	check_livelist_gone
	log_must zfs destroy -R $TESTPOOL/$TESTCLONE

	# Threshold set to 20 percent
	set_tunable32 LIVELIST_MIN_PERCENT_SHARED 20
	clone_dataset $TESTFS1 snap $TESTCLONE

	log_must mkfile 5m /$TESTPOOL/$TESTCLONE/$TESTFILE0
	log_must mkfile 5m /$TESTPOOL/$TESTCLONE/$TESTFILE1
	log_must mkfile 5m /$TESTPOOL/$TESTCLONE/$TESTFILE2
	sync_pool $TESTPOOL
	# snapshot and clone share 'atestfile', 25 percent
	check_livelist_exists $TESTCLONE
	log_must rm /$TESTPOOL/$TESTCLONE/atestfile
	# snapshot and clone share no files
	check_livelist_gone
	log_must zfs destroy -R $TESTPOOL/$TESTCLONE
}

ORIGINAL_MAX=$(get_tunable LIVELIST_MAX_ENTRIES)
ORIGINAL_MIN=$(get_tunable LIVELIST_MIN_PERCENT_SHARED)

log_onexit cleanup
# You might think that setting compression=off for $TESTFS1 would be
# sufficient. You would be mistaken.
# You need compression=off for whatever the parent of $TESTFS1 is,
# and $TESTFS1.
log_must zfs set compression=off $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS1
log_must mkfile 5m /$TESTPOOL/$TESTFS1/atestfile
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap
test_condense
test_deactivated

log_pass "Clone's livelist condenses and disables as expected."
