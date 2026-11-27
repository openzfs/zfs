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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026, Rick Galliher.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Replay of a record for an object that an earlier record removed must
#	not fail.
#
#	Writes, truncates, attribute and xattr changes are logged with their
#	own object and can reach the log after the record that removed it.
#	Replay skips such a record if its object does not exist.  But the
#	removal may still be in progress then: the znode is released by a
#	taskq, and the xattr directory of a removed file and its files are
#	released after that.  The replay function then failed with ENOENT,
#	was tried again without another look at the object, failed again,
#	and a failed record ends replay: everything logged after it was lost.
#
#	Whether the removal is in progress is a race, so this test repeats.
#	Without the fix some of the repetitions fail.
#
# STRATEGY:
#	1. Create a file system with xattr=dir
#	2. Stop txgs from syncing on their own
#	3. Create files with an xattr each, which leaves a truncate (on
#	   FreeBSD, an attribute change) of each xattr file waiting, and
#	   rename another file over each: the xattr files go with the
#	   replaced files
#	4. Create a few directories, so that the removals have started when
#	   replay reaches the xattr files' records, and commit everything
#	   with sync
#	5. Create, write and fsync another file
#	6. Save the vdevs, restore them and import the pool to replay the log.
#	   The copy is what a crash leaves; exporting a frozen pool instead
#	   would first commit every outstanding record
#	7. Verify the other file
#	8. Repeat 3 to 7
#

verify_runnable "global"

function cleanup_fs
{
	restore_tunable TXG_TIMEOUT
	cleanup
	rm -f $VDIR2/a.save $VDIR2/e.save $VDIR2/zil.out
}

# Create the ZIL header, so that a later fsync does not sync a txg, and
# make everything so far part of the synced state.
function settle
{
	log_must dd if=/dev/zero of=$fs/sync oflag=sync bs=1 count=1
	log_must sync_pool $TESTPOOL
}

# Another file, created, written and fsynced after the records under test.
# Nothing is read back from the file system before the vdevs are saved:
# looking for holes in a file with unsynced data syncs a txg.
function write_later
{
	log_must dd if=/dev/urandom of=$TESTDIR/later bs=4k count=1
	log_must dd if=$TESTDIR/later of=$fs/later bs=4k count=1 conv=fsync
}

# Save the vdevs, which hold the log but not the changes, as after a crash.
# Then put them back and import the pool, which replays the log.
function crash_and_replay # record
{
	typeset record=$1
	typeset synced

	log_must cp ${vdevs[0]} $VDIR2/a.save
	log_must cp ${vdevs[1]} $VDIR2/e.save
	log_must zpool export $TESTPOOL
	log_must cp $VDIR2/a.save ${vdevs[0]}
	log_must cp $VDIR2/e.save ${vdevs[1]}
	# The test means nothing unless the record under test is still to be
	# replayed: in the log, and of a txg that has not synced.  A txg
	# that syncs before the vdevs are saved fails the test here.
	synced=$(zdb -e -p $VDIR -u $TESTPOOL | awk '$1 == "txg" { print $3 }')
	log_must test -n "$synced"
	log_must eval "zdb -e -p $VDIR -ivv $TESTPOOL/$TESTFS > $VDIR2/zil.out"
	if ! awk -v synced=$synced \
	    '$1 == "'$record'" && $2 == "len" && int($5) > synced { n++ }
	    END { exit n == 0 }' $VDIR2/zil.out; then
		cat $VDIR2/zil.out
		log_fail "the saved vdevs hold no $record record to replay"
	fi
	log_must zpool import -d $VDIR $TESTPOOL
	# Replay stops at a record it cannot apply: what follows is lost.
	log_must cmp $fs/later $TESTDIR/later
	log_must rm $fs/later
}

log_assert "Replay of a record for a removed object does not fail."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT

typeset fs=/$TESTPOOL/$TESTFS
set -A vdevs $VDIR/a $VDIR/e
typeset -i round=0 i
# The record of an xattr file that is waiting when its file is replaced.
typeset record=TX_TRUNCATE
is_freebsd && record=TX_SETATTR

#
# 1. Every xattr is a file in the xattr directory.
#
log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o xattr=dir $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR

#
# 2. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

while (( round < 12 )); do
	settle

	#
	# 3. Each replaced file takes an xattr file with it.
	#
	i=0
	while (( i < 8 )); do
		log_must touch $fs/victim.$round.$i $fs/new.$round.$i
		log_must set_xattr name value $fs/victim.$round.$i
		log_must mv $fs/new.$round.$i $fs/victim.$round.$i
		(( i += 1 ))
	done

	#
	# 4. The number of directories varies the time the removals have.
	#
	i=0
	while (( i < round % 4 )); do
		log_must mkdir $fs/dir.$round.$i
		(( i += 1 ))
	done
	if is_linux; then
		log_must sync -f $fs
	else
		log_must sync
	fi

	#
	# 5-7.
	#
	write_later
	crash_and_replay $record
	(( round += 1 ))
done

log_pass "Replay of a record for a removed object does not fail."
