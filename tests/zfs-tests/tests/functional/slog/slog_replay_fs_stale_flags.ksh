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
#	Replay of a remove, a rename, a link or a write must not fail
#	because of a flag or a project ID that was changed before it.
#
#	zfs_remove() and zfs_rename() refuse an immutable file, as FreeBSD's
#	zfs_link() does; on FreeBSD they also refuse an append-only file and
#	an entry of an append-only directory.  zfs_rename() and zfs_link()
#	refuse a move or a link into a directory that inherits another
#	project ID.  The flag
#	and the project ID are changed by TX_SETATTR records,
#	which are committed with their own object.  The remove, rename or
#	link is committed by any commit, so the log can hold it without the
#	TX_SETATTR that made it legal.  Replay then failed, and a failed
#	record ends replay: everything logged after it was lost.
#
#	zfs_write() refuses an immutable file, and a write below the end of
#	an append-only one.  A page changed through a mapping is written,
#	and logged, after the flag is set, and replay of that write failed.
#	Software that replays an indirect write as its whole block failed
#	an ordinary append to an append-only file too.
#
# STRATEGY:
#	1. Create a file system with immutable files and a directory, a
#	   directory that inherits a project ID, and append-only files and
#	   a directory
#	2. Stop txgs from syncing on their own
#	3. Clear the flag and remove, replace, link or move the object;
#	   or change a file through a mapping, set the flag and fsync the
#	   file; or append.  Then create, write and fsync another file
#	4. Save the vdevs, restore them and import the pool to replay the log
#	5. Verify the other file, the name space and the contents
#	6. Repeat 3 to 5 for each case
#

verify_runnable "global"

function cleanup_fs
{
	restore_tunable TXG_TIMEOUT
	cleanup
	rm -f $VDIR2/a.save $VDIR2/e.save
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
function crash_and_replay
{
	log_must cp ${vdevs[0]} $VDIR2/a.save
	log_must cp ${vdevs[1]} $VDIR2/e.save
	log_must zpool export $TESTPOOL
	log_must cp $VDIR2/a.save ${vdevs[0]}
	log_must cp $VDIR2/e.save ${vdevs[1]}
	# The test means nothing unless the log is still to be replayed.
	zdb -e -p $VDIR -ivv $TESTPOOL/$TESTFS | grep "TX_.*len" || \
	    log_fail "the saved vdevs hold no log records"
	log_must zpool import -d $VDIR $TESTPOOL
	# Replay stops at a record it cannot apply: what follows is lost.
	log_must cmp $fs/later $TESTDIR/later
	log_must rm $fs/later
}

# Change a file through a shared mapping, set a flag on the file while the
# page is still dirty, and fsync the file: the page is written back, and
# logged, after the flag is set.
function mapped_write_then_flag # file flag
{
	python3 -c '
import mmap, os, subprocess, sys
fd = os.open(sys.argv[1], os.O_RDWR)
m = mmap.mmap(fd, 0)
m[0:6] = b"mapped"
subprocess.run([sys.argv[3], sys.argv[2], sys.argv[1]], check=True)
os.fsync(fd)' $1 $2 $chflag
}

# Append a file to another and fsync it.
function append_fsync # source file
{
	python3 -c '
import os, sys
data = open(sys.argv[1], "rb").read()
fd = os.open(sys.argv[2], os.O_WRONLY | os.O_APPEND)
os.write(fd, data)
os.fsync(fd)' $1 $2
}

# The command that changes file flags, and its names for them.
if is_freebsd; then
	typeset chflag=chflags immutable=schg mutable=noschg appendonly=sappnd
	typeset notappendonly=nosappnd
else
	typeset chflag=chattr immutable=+i mutable=-i appendonly=+a
	typeset notappendonly=-a
fi

log_assert "Replay of a remove, rename, link or write does not fail" \
    "because of a flag or project ID changed before it."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT

typeset fs=/$TESTPOOL/$TESTFS
set -A vdevs $VDIR/a $VDIR/e

#
# 1. Immutable files and directory, a project directory, and append-only
#    files and directory.
#
log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR
log_must mkdir $fs/removed_dir $fs/project_dir
log_must touch $fs/removed $fs/replaced $fs/moved $fs/linked $fs/freed
log_must $chflag $immutable $fs/removed $fs/replaced $fs/freed
log_must $chflag $immutable $fs/removed_dir
log_must zfs project -s -p 5 $fs/project_dir
log_must dd if=/dev/urandom of=$TESTDIR/mapped bs=4k count=2
log_must cp $TESTDIR/mapped $fs/mapped_immutable
log_must cp $TESTDIR/mapped $fs/mapped_appendonly
log_must cp $TESTDIR/mapped $TESTDIR/mapped.want
log_must eval "printf mapped | dd of=$TESTDIR/mapped.want conv=notrunc"
log_must dd if=/dev/urandom of=$TESTDIR/appended bs=1000 count=1
log_must cp $TESTDIR/appended $fs/appended
log_must $chflag $appendonly $fs/appended
log_must mkdir $fs/appendonly_dir
log_must touch $fs/appendonly_removed $fs/appendonly_dir/entry
log_must $chflag $appendonly $fs/appendonly_removed $fs/appendonly_dir

#
# 2. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 3-5. Remove a file that was immutable.
#
settle
log_must $chflag $mutable $fs/removed
log_must rm $fs/removed
write_later
crash_and_replay
log_mustnot ls $fs/removed

#
# 3-5. Replace a file that was immutable.
#
settle
log_must $chflag $mutable $fs/replaced
log_must dd if=/dev/urandom of=$TESTDIR/new bs=4k count=1
log_must dd if=$TESTDIR/new of=$fs/new bs=4k count=1
log_must mv $fs/new $fs/replaced
write_later
crash_and_replay
log_must cmp $fs/replaced $TESTDIR/new
log_mustnot ls $fs/new

#
# 3-5. Remove a directory that was immutable.
#
settle
log_must $chflag $mutable $fs/removed_dir
log_must rmdir $fs/removed_dir
write_later
crash_and_replay
log_mustnot ls -d $fs/removed_dir

#
# 3-5. Remove a file that was append-only, and an entry of a directory
#      that was append-only.
#
settle
log_must $chflag $notappendonly $fs/appendonly_removed
log_must rm $fs/appendonly_removed
write_later
crash_and_replay
log_mustnot ls $fs/appendonly_removed

settle
log_must $chflag $notappendonly $fs/appendonly_dir
log_must rm $fs/appendonly_dir/entry
write_later
crash_and_replay
log_mustnot ls $fs/appendonly_dir/entry

#
# 3-5. Link a file that was immutable.
#
settle
log_must $chflag $mutable $fs/freed
log_must ln $fs/freed $fs/freed_link
write_later
crash_and_replay
log_must ls $fs/freed_link

#
# 3-5. Change a file through a mapping, and make it immutable or
#      append-only before the page is written back.
#
settle
log_must mapped_write_then_flag $fs/mapped_immutable $immutable
write_later
crash_and_replay
log_must cmp $fs/mapped_immutable $TESTDIR/mapped.want

settle
log_must mapped_write_then_flag $fs/mapped_appendonly $appendonly
write_later
crash_and_replay
log_must cmp $fs/mapped_appendonly $TESTDIR/mapped.want

#
# 3-5. Append to an append-only file with an indirect write, which is
#      logged as a block that starts below the end of the file.
#
log_must zfs set logbias=throughput $TESTPOOL/$TESTFS
settle
log_must dd if=/dev/urandom of=$TESTDIR/more bs=5000 count=1
log_must append_fsync $TESTDIR/more $fs/appended
write_later
crash_and_replay
log_must eval "cat $TESTDIR/more >> $TESTDIR/appended"
log_must cmp $fs/appended $TESTDIR/appended
log_must zfs inherit logbias $TESTPOOL/$TESTFS

#
# 3-5. Move a file into a directory that inherited a project ID.
#
settle
log_must zfs project -C -k $fs/project_dir
log_must mv $fs/moved $fs/project_dir/moved
write_later
crash_and_replay
log_must ls $fs/project_dir/moved
log_mustnot ls $fs/moved

#
# 3-5. Give a file the project ID of a directory that inherits it, and
#      link the file there.
#
log_must zfs project -s -p 5 $fs/project_dir
settle
log_must zfs project -p 5 $fs/linked
log_must ln $fs/linked $fs/project_dir/link
write_later
crash_and_replay
log_must ls $fs/project_dir/link

log_pass "Replay of a remove, rename, link or write does not fail" \
    "because of a flag or project ID changed before it."
