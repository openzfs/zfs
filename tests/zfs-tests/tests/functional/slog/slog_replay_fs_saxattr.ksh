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
#	Replay of an SA xattr record (TX_SETSAXATTR) must not fail, and must
#	keep an empty value.
#
#	A record with a size of zero removes the xattr.  It was also logged
#	when an xattr was set to an empty value and, on Linux, when an xattr
#	kept in the xattr directory of a file on an xattr=sa dataset was
#	removed.  The system attributes then did not hold the name, replay
#	failed with ENOENT, and a failed record ends replay: everything
#	logged after it was lost.  An existing xattr set to an empty value
#	was removed.
#
# STRATEGY:
#	1. Create a file system with xattr=sa, and a file with an xattr too
#	   large to be a system attribute and one that is not
#	2. Stop txgs from syncing on their own
#	3. Change an xattr and fsync the file; then create, write and fsync
#	   another file
#	4. Save the vdevs, restore them and import the pool to replay the log.
#	   The copy is what a crash leaves; exporting a frozen pool instead
#	   would first commit every outstanding record
#	5. Verify the other file and the xattr
#	6. Repeat 3 to 5 for each change: removing the large xattr, setting
#	   a new xattr to an empty value, emptying an existing xattr,
#	   removing an xattr that was set while SA xattrs were not logged
#	7. Repeat 3 to 5 once more with removals, empty values and other
#	   values for names of eight consecutive lengths: the length of
#	   the record, which is rounded up to eight bytes, is what tells an
#	   empty value from a removal
#

verify_runnable "global"

function cleanup_fs
{
	restore_tunable TXG_TIMEOUT
	restore_tunable ZIL_SAXATTR
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
function crash_and_replay # record [xattr name]
{
	typeset record=$1
	typeset name=$2
	typeset synced

	log_must cp ${vdevs[0]} $VDIR2/a.save
	log_must cp ${vdevs[1]} $VDIR2/e.save
	log_must zpool export $TESTPOOL
	log_must cp $VDIR2/a.save ${vdevs[0]}
	log_must cp $VDIR2/e.save ${vdevs[1]}
	# The test means nothing unless the record under test is still to be
	# replayed: in the log, and of a txg that has not synced.  A txg
	# that syncs before the vdevs are saved fails the test here.  An SA
	# xattr record must be the named xattr's: SELinux labels the other
	# file with one of its own.
	synced=$(zdb -e -p $VDIR -u $TESTPOOL | awk '$1 == "txg" { print $3 }')
	log_must test -n "$synced"
	log_must eval "zdb -e -p $VDIR -ivv $TESTPOOL/$TESTFS > $VDIR2/zil.out"
	if ! awk -v synced=$synced -v name="$name" '
	    $2 == "len" { fresh = ($1 == "'$record'" && int($5) > synced) }
	    fresh && (name == "" || ($1 == "XAT_NAME" && index($2, name))) {
		n++; fresh = 0 }
	    END { exit n == 0 }' $VDIR2/zil.out; then
		cat $VDIR2/zil.out
		log_fail "the saved vdevs hold no $record $name record to replay"
	fi
	log_must zpool import -d $VDIR $TESTPOOL
	# Replay stops at a record it cannot apply: what follows is lost.
	log_must cmp $fs/later $TESTDIR/later
	log_must rm $fs/later
}

log_assert "Replay of an SA xattr record does not fail, and keeps an empty" \
    "value."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT
log_must save_tunable ZIL_SAXATTR

typeset fs=/$TESTPOOL/$TESTFS
typeset file=$fs/file
set -A vdevs $VDIR/a $VDIR/e

#
# 1. A value over 32K goes to the xattr directory.
#
log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o xattr=sa $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR
log_must touch $file
log_must dd if=/dev/zero of=$TESTDIR/large bs=40000 count=1
log_must set_xattr_stdin large $file < $TESTDIR/large
log_must set_xattr small value $file

#
# 2. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 3-5. Remove the xattr that is in the xattr directory.
#
settle
log_must rm_xattr large $file
log_must dd if=/dev/zero of=$file bs=1 count=1 conv=notrunc,fsync
write_later
crash_and_replay TX_REMOVE
# No SA removal is logged for a name the SA does not hold.  Other SA
# records can be there: SELinux labels the other file when it is created.
log_mustnot grep "XAT_NAME .*large" $VDIR2/zil.out
log_mustnot get_xattr large $file
log_must test "$(get_xattr small $file)" = value

#
# 3-5. Set a new xattr to an empty value.
#
settle
log_must set_xattr new "" $file
log_must dd if=/dev/zero of=$file bs=1 count=1 conv=notrunc,fsync
write_later
crash_and_replay TX_SETSAXATTR new
log_must get_xattr new $file
log_must test -z "$(get_xattr new $file)"

#
# 3-5. Empty an existing xattr.
#
settle
log_must set_xattr small "" $file
log_must dd if=/dev/zero of=$file bs=1 count=1 conv=notrunc,fsync
write_later
crash_and_replay TX_SETSAXATTR small
log_must get_xattr small $file
log_must test -z "$(get_xattr small $file)"

#
# 3-5. Remove an xattr that was set while SA xattrs were not logged: the
#      log holds the removal of a name that replay does not find.
#
settle
log_must set_tunable32 ZIL_SAXATTR 0
log_must set_xattr unlogged value $file
log_must set_tunable32 ZIL_SAXATTR 1
log_must rm_xattr unlogged $file
log_must dd if=/dev/zero of=$file bs=1 count=1 conv=notrunc,fsync
write_later
crash_and_replay TX_SETSAXATTR unlogged
log_mustnot get_xattr unlogged $file

#
# 7. Names of eight consecutive lengths end, with their NUL, before, at and
#    after a multiple of eight bytes, whatever prefix they are stored with.
#
typeset pad=2345678
typeset -i len
typeset tail name
for len in 1 2 3 4 5 6 7 8; do
	tail=${pad:0:$((len - 1))}
	log_must set_xattr r$tail value $file
	log_must set_xattr e$tail value $file
	log_must set_xattr t$tail value $file
done
settle
for len in 1 2 3 4 5 6 7 8; do
	tail=${pad:0:$((len - 1))}
	# Remove an xattr.
	log_must rm_xattr r$tail $file
	# Set a new xattr to an empty value.
	log_must set_xattr n$tail "" $file
	# Empty an existing xattr.
	log_must set_xattr e$tail "" $file
	# Set an xattr and remove it.
	log_must set_xattr s$tail value $file
	log_must rm_xattr s$tail $file
	# Remove an xattr and set it again, to an empty value.
	log_must rm_xattr t$tail $file
	log_must set_xattr t$tail "" $file
	# Set an xattr to an empty value and then to another.
	log_must set_xattr u$tail "" $file
	log_must set_xattr u$tail value $file
done
log_must dd if=/dev/zero of=$file bs=1 count=1 conv=notrunc,fsync
write_later
# The last of them.
crash_and_replay TX_SETSAXATTR u$pad
for len in 1 2 3 4 5 6 7 8; do
	tail=${pad:0:$((len - 1))}
	log_mustnot get_xattr r$tail $file
	log_mustnot get_xattr s$tail $file
	for name in n$tail e$tail t$tail; do
		log_must get_xattr $name $file
		log_must test -z "$(get_xattr $name $file)"
	done
	log_must test "$(get_xattr u$tail $file)" = value
done

log_pass "Replay of an SA xattr record does not fail, and keeps an empty" \
    "value."
