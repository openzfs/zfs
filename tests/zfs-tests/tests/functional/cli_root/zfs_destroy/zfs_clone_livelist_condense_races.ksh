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
# Test race conditions for livelist condensing

# STRATEGY
# These tests exercise code paths that deal with a livelist being
# simultaneously condensed and deactivated (deleted, exported or disabled).
# If a variable is set, the zthr will pause until it is cancelled or waited
# and then a counter variable keeps track of whether or not the code path is
# reached.

# 1. Deletion race: repeatedly overwrite the same file to trigger condense
# and then delete the clone.
# 2. Disable race: Overwrite enough files to trigger condenses and disabling of
# the livelist.
# 3. Export race: repeatedly overwrite the same file to trigger condense and
# then export the pool.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

function cleanup
{
	log_must zfs destroy -Rf $TESTPOOL/$TESTFS1
	# reset the livelist sublist size to the original value
	set_tunable64 LIVELIST_MAX_ENTRIES $ORIGINAL_MAX
	# reset the condense tests to 0
	set_tunable32 LIVELIST_CONDENSE_ZTHR_PAUSE 0
	set_tunable32 LIVELIST_CONDENSE_SYNC_PAUSE 0
}

function delete_race
{
	set_tunable32 "$1" 0
	log_must zfs clone $TESTPOOL/$TESTFS1@snap $TESTPOOL/$TESTCLONE
	for i in {1..5}; do
		sync_pool $TESTPOOL
		log_must mkfile 5m /$TESTPOOL/$TESTCLONE/out
	done
	log_must zfs destroy $TESTPOOL/$TESTCLONE
	sync_pool $TESTPOOL
	[[ "1" == "$(get_tunable "$1")" ]] || \
	    log_fail "delete/condense race test failed"
}

function export_race
{
	set_tunable32 "$1" 0
	log_must zfs clone $TESTPOOL/$TESTFS1@snap $TESTPOOL/$TESTCLONE
	for i in {1..5}; do
		sync_pool $TESTPOOL
		log_must mkfile 5m /$TESTPOOL/$TESTCLONE/out
	done
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	[[ "1" == "$(get_tunable "$1")" ]] || \
	    log_fail "export/condense race test failed"
	log_must zfs destroy $TESTPOOL/$TESTCLONE
}

function disable_race
{
	set_tunable32 "$1" 0
	log_must zfs clone $TESTPOOL/$TESTFS1@snap $TESTPOOL/$TESTCLONE
	for i in {1..5}; do
		sync_pool $TESTPOOL
		log_must mkfile 5m /$TESTPOOL/$TESTCLONE/out
	done
	# overwrite the file shared with the origin to trigger disable
	log_must mkfile 100m /$TESTPOOL/$TESTCLONE/atestfile
	sync_pool $TESTPOOL
	[[ "1" == "$(get_tunable "$1")" ]] || \
	    log_fail "disable/condense race test failed"
	log_must zfs destroy $TESTPOOL/$TESTCLONE
}

ORIGINAL_MAX=$(get_tunable LIVELIST_MAX_ENTRIES)

log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS1
log_must mkfile 100m /$TESTPOOL/$TESTFS1/atestfile
sync_pool $TESTPOOL
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap

# Reduce livelist size to trigger condense more easily
set_tunable64 LIVELIST_MAX_ENTRIES 20

# Test cancellation path in the zthr
set_tunable32 LIVELIST_CONDENSE_ZTHR_PAUSE 1
set_tunable32 LIVELIST_CONDENSE_SYNC_PAUSE 0
disable_race LIVELIST_CONDENSE_ZTHR_CANCEL
delete_race LIVELIST_CONDENSE_ZTHR_CANCEL
export_race LIVELIST_CONDENSE_ZTHR_CANCEL

# Test cancellation path in the synctask
set_tunable32 LIVELIST_CONDENSE_ZTHR_PAUSE 0
set_tunable32 LIVELIST_CONDENSE_SYNC_PAUSE 1
disable_race LIVELIST_CONDENSE_SYNC_CANCEL
delete_race LIVELIST_CONDENSE_SYNC_CANCEL

log_pass "Clone livelist condense race conditions passed."
