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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	With acltype=posix and SA xattr logging, a new file, directory or
#	FIFO survives a crash with the mode and the ACLs it inherited once
#	its parent directory has been fsynced, and so does a new directory
#	once it has been fsynced itself.
#
# STRATEGY:
#	For xattr=dir and xattr=sa, on a pool of its own:
#	1. Create a directory whose default ACL is equivalent to a mode and
#	   one whose default ACL names a user, and sync.  Freeze the pool.
#	2. Create a file, a directory and a FIFO under each, and fsync
#	   each parent directory only.  Then create another directory
#	   under each and fsync that directory only.
#	3. Copy the pool's device, as a crash would leave it.  An export
#	   would commit the rest of the log.
#	4. Import the copy, which replays its log.  Verify that the new
#	   entries have their modes and ACLs.
#

verify_runnable "global"

typeset pool=$TESTPOOL1
typeset vdir

function cleanup
{
	poolexists $pool && destroy_pool $pool
	[[ -n $vdir ]] && log_must rm -rf $vdir
}

function fsync_path # path
{
	log_must python3 -c \
	    "import os; os.fsync(os.open('$1', os.O_RDONLY))"
}

function test_crash # xattr
{
	typeset d=/$pool
	typeset objs="" sub obj now changed=""
	typeset -A mode acl

	log_must truncate -s 256m $vdir/live
	log_must zpool create -O acltype=posix -O xattr=$1 $pool $vdir/live
	require_saxattr_logging $pool
	# The first commit creates the log and waits for its TXG.
	log_must dd if=/dev/zero of=$d/seed bs=1 count=1 conv=fsync
	log_must mkdir $d/equiv $d/named
	log_must setfacl -d -m u::rwx,g::rwx,o::--- $d/equiv
	log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx,g::r-x,o::--- $d/named
	sync_pool $pool
	log_must zpool freeze $pool

	for sub in equiv named; do
		log_must dd if=/dev/zero of=$d/$sub/file bs=1 count=1
		log_must makedir $d/$sub/dir
		log_must python3 -c \
		    "import os; os.mkfifo('$d/$sub/fifo', 0o666)"
		fsync_path $d/$sub
		log_must makedir $d/$sub/dir2
		fsync_path $d/$sub/dir2
		objs="$objs $sub/file $sub/dir $sub/fifo $sub/dir2"
	done
	for obj in $objs; do
		mode[$obj]=$(stat -c %a $d/$obj) ||
		    log_fail "xattr=$1: stat $obj failed"
		acl[$obj]=$(getfacl -cpn $d/$obj) ||
		    log_fail "xattr=$1: getfacl $obj failed"
	done

	log_must cp $vdir/live $vdir/crash
	log_must zpool export $pool
	log_must zpool import -d $vdir/crash $pool

	for obj in $objs; do
		now=$(stat -c %a $d/$obj) ||
		    log_fail "xattr=$1: stat $obj failed after replay"
		[[ "$now" == "${mode[$obj]}" ]] ||
		    changed="$changed $obj(mode $now)"
		now=$(getfacl -cpn $d/$obj 2>/dev/null) || now=unreadable
		[[ "$now" == "${acl[$obj]}" ]] || changed="$changed $obj(ACL)"
	done
	log_must zpool destroy $pool
	log_must rm -f $vdir/live $vdir/crash
	[[ -z $changed ]] || log_fail "xattr=$1: replay changed$changed"
}

log_assert "ACLs inherited at creation survive a crash with the entry"
log_onexit cleanup
require_saxattr_logging

vdir=$(mktemp -d $TEST_BASE_DIR/posix_008.XXXXXX) || log_fail "mktemp failed"
umask 022
test_crash dir
test_crash sa

log_pass "ACLs inherited at creation survive a crash with the entry"
