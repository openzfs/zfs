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
#	With acltype=posix and SA xattr logging, the mode that setfacl
#	gives an existing file or directory survives a crash with its
#	access ACL once the file has been fsynced.  An ACL removal that a
#	commit for another file writes does not leave the ACL's mask, or an
#	earlier chmod, as the owning group's permissions.
#
# STRATEGY:
#	For xattr=dir and xattr=sa, on a pool of its own:
#	1. Create the objects below and sync.  Freeze the pool.
#	2. Give a file without an ACL one that names a user and another one
#	   equivalent to a mode, change the mask of an ACL, and remove
#	   another ACL.  fsync each of them.
#	3. On two 0600 files, add an ACL that grants the owning group's
#	   entry nothing and fsync; then remove the ACL, on one of them
#	   after a chmod.  Do not fsync them.
#	4. fsync an unrelated file and copy the pool's device, as a crash
#	   would leave it.  An export would commit the rest of the log.
#	5. Import the copy, which replays its log.  Verify that the objects
#	   of step 2 have their modes and ACLs, and that the files of step 3
#	   have an ACL or mode 0600.
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
	typeset objs="add equiv modify remove"
	typeset want="640 664 660 600"
	typeset obj now modes="" changed=""
	typeset -A mode acl

	log_must truncate -s 256m $vdir/live
	log_must zpool create -O acltype=posix -O xattr=$1 $pool $vdir/live
	# The first commit creates the log and waits for its TXG.
	log_must dd if=/dev/zero of=$d/seed bs=1 count=1 conv=fsync
	log_must touch $d/add $d/equiv $d/modify $d/remove $d/other
	log_must touch $d/promote $d/promote-chmod
	log_must chmod 644 $d/add $d/equiv
	log_must chmod 640 $d/modify
	log_must chmod 600 $d/remove $d/promote $d/promote-chmod
	log_must setfacl -m u:$ZFS_ACL_STAFF1:r-- $d/modify
	log_must setfacl -m u:$ZFS_ACL_STAFF1:rwx $d/remove
	sync_pool $pool
	log_must zpool freeze $pool

	log_must setfacl -m u:$ZFS_ACL_STAFF1:r--,m::r--,o::--- $d/add
	log_must setfacl -m g::rw- $d/equiv
	log_must setfacl -m u:$ZFS_ACL_STAFF1:rw- $d/modify
	log_must setfacl -b $d/remove
	for obj in $objs; do
		mode[$obj]=$(stat -c %a $d/$obj) ||
		    log_fail "xattr=$1: stat $obj failed"
		acl[$obj]=$(getfacl -cpn $d/$obj) ||
		    log_fail "xattr=$1: getfacl $obj failed"
		modes="$modes ${mode[$obj]}"
		fsync_path $d/$obj
	done
	[[ "${modes# }" == "$want" ]] ||
	    log_fail "xattr=$1: modes$modes of $objs, expected $want"

	for obj in promote promote-chmod; do
		log_must setfacl -m u:$ZFS_ACL_STAFF1:rwx $d/$obj
		fsync_path $d/$obj
		[[ $obj == promote-chmod ]] && log_must chmod 600 $d/$obj
		log_must setfacl -b $d/$obj
		now=$(stat -c %a $d/$obj)
		[[ $now == 600 ]] ||
		    log_fail "xattr=$1: $obj is $now before the crash"
	done

	log_must dd if=/dev/zero of=$d/other bs=1 count=1 conv=notrunc,fsync
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
	for obj in promote promote-chmod; do
		now=$(getfacl -cpn $d/$obj 2>/dev/null) || now=unreadable
		[[ "$now" == *mask::* ]] && continue
		now=$(stat -c %a $d/$obj)
		[[ $now == 600 ]] || changed="$changed $obj(mode $now, no ACL)"
	done
	log_must zpool destroy $pool
	log_must rm -f $vdir/live $vdir/crash
	[[ -z $changed ]] || log_fail "xattr=$1: replay changed$changed"
}

log_assert "Modes set by setfacl survive a crash with their ACLs"
log_onexit cleanup

vdir=$(mktemp -d $TEST_BASE_DIR/posix_007.XXXXXX) || log_fail "mktemp failed"
umask 022
test_crash dir
test_crash sa

log_pass "Modes set by setfacl survive a crash with their ACLs"
