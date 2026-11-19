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
#	With acltype=posix, the mode that the umask or an inherited default
#	ACL gives a new file, directory or FIFO survives ZIL replay.
#
# STRATEGY:
#	For xattr=dir and xattr=sa:
#	1. Create a directory whose default ACL is equivalent to a mode and
#	   one whose default ACL names a user, and sync.
#	2. Freeze the pool.
#	3. Create a file, a directory and a FIFO under umask 022 and under
#	   each default ACL.
#	4. Check the modes, and record them and the ACLs.
#	5. Export and import the pool, which replays the log.
#	6. Verify that the modes and ACLs are unchanged.
#

verify_runnable "global"

typeset -i frozen=0

function cleanup
{
	if ((frozen)); then
		log_must zpool export $TESTPOOL
		log_must zpool import $TESTPOOL
	fi
	log_must zfs inherit sync $TESTPOOL/$TESTFS
	log_must zfs inherit xattr $TESTPOOL/$TESTFS
	log_must rm -rf $TESTDIR/replay.dir $TESTDIR/replay.sa
}

function test_replay # xattr
{
	typeset dir=$TESTDIR/replay.$1
	typeset objs="" sub obj now modes="" changed=""
	typeset want="644 755 644 660 770 660 664 775 664"
	typeset -A mode acl

	log_must zfs set xattr=$1 $TESTPOOL/$TESTFS
	log_must mkdir $dir $dir/equiv $dir/named
	log_must setfacl -d -m u::rwx,g::rwx,o::--- $dir/equiv
	log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx $dir/named
	# The first commit creates the log and waits for its TXG.
	log_must dd if=/dev/zero of=$dir/seed bs=1 count=1 conv=fsync
	sync_pool $TESTPOOL
	log_must zfs set sync=always $TESTPOOL/$TESTFS
	log_must zpool freeze $TESTPOOL
	frozen=1

	for sub in "" equiv/ named/; do
		# open(2), mkdir(2) and mknod(2) with no chmod(2)
		log_must dd if=/dev/zero of=$dir/${sub}file bs=1 count=1
		log_must makedir $dir/${sub}dir
		log_must python3 -c \
		    "import os; os.mkfifo('$dir/${sub}fifo', 0o666)"
		objs="$objs ${sub}file ${sub}dir ${sub}fifo"
	done

	for obj in $objs; do
		mode[$obj]=$(stat -c %a $dir/$obj) ||
		    log_fail "xattr=$1: stat $obj failed"
		acl[$obj]=$(getfacl -cpn $dir/$obj) ||
		    log_fail "xattr=$1: getfacl $obj failed"
		modes="$modes ${mode[$obj]}"
	done
	[[ "${modes# }" == "$want" ]] ||
	    log_fail "xattr=$1: modes$modes of$objs, expected $want"

	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	frozen=0

	for obj in $objs; do
		now=$(stat -c %a $dir/$obj) ||
		    log_fail "xattr=$1: stat $obj failed after replay"
		[[ "$now" == "${mode[$obj]}" ]] ||
		    changed="$changed $obj(mode $now)"
		now=$(getfacl -cpn $dir/$obj) ||
		    log_fail "xattr=$1: getfacl $obj failed after replay"
		[[ "$now" == "${acl[$obj]}" ]] || changed="$changed $obj(ACL)"
	done
	[[ -z $changed ]] || log_fail "xattr=$1: replay changed$changed"

	log_must zfs inherit sync $TESTPOOL/$TESTFS
}

log_assert "Modes set by the umask or a default ACL survive ZIL replay"
log_onexit cleanup

umask 022
test_replay dir
test_replay sa

log_pass "Modes set by the umask or a default ACL survive ZIL replay"
