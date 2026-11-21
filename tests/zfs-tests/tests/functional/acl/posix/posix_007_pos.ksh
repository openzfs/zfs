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
#	earlier chmod, as the owning group's permissions, and an ACL that
#	such a commit writes does not come back without an earlier chgrp.
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
#	4. Change the group of a file with an ACL, then give the owning
#	   group's entry write permission.  Do not fsync it.
#	5. fsync an unrelated file and copy the pool's device, as a crash
#	   would leave it.  An export would commit the rest of the log.
#	With xattr=sa, an ACL stored while the dataset had xattr=dir is also
#	removed and fsynced, ahead of the objects of step 2: it is kept in
#	the xattr directory, and its removal must not stop the replay of
#	what follows.
#	6. Import the copy, which replays its log.  Verify that the objects
#	   of step 2 have their modes and ACLs, that each file of step 3
#	   has a mode and ACL it had since its fsync, and that the file of
#	   step 4 has a group and ACL it had together.
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

# Print what stat reports for the format, then the ACL.  Fail if either
# cannot be read: an unreadable ACL is not a state.
function acl_state # stat-format path
{
	typeset s a

	s=$(stat -c "$1" $2) || return 1
	a=$(getfacl -cpn $2 2>/dev/null) || return 1
	echo "$s $a"
}

function test_crash # xattr
{
	typeset d=/$pool
	typeset objs="add equiv modify remove"
	typeset want="640 664 660 600"
	typeset obj now modes="" changed=""
	typeset -A mode acl had

	log_must truncate -s 256m $vdir/live
	log_must zpool create -O acltype=posix -O xattr=$1 $pool $vdir/live
	require_saxattr_logging $pool
	# The first commit creates the log and waits for its TXG.
	log_must dd if=/dev/zero of=$d/seed bs=1 count=1 conv=fsync
	log_must touch $d/add $d/equiv $d/modify $d/remove $d/other
	log_must touch $d/promote $d/promote-chmod $d/regroup
	log_must chmod 644 $d/add $d/equiv
	log_must chmod 640 $d/modify $d/regroup
	log_must chmod 600 $d/remove $d/promote $d/promote-chmod
	log_must setfacl -m u:$ZFS_ACL_STAFF1:r-- $d/modify
	log_must setfacl -m u:$ZFS_ACL_STAFF1:rwx $d/remove
	log_must setfacl -m u:$ZFS_ACL_STAFF1:r-- $d/regroup
	if [[ $1 == sa ]]; then
		log_must zfs set xattr=dir $pool
		log_must touch $d/moved
		log_must setfacl -m u:$ZFS_ACL_STAFF1:rwx $d/moved
		log_must zfs set xattr=sa $pool
		objs="moved $objs"
		want="644 $want"
	fi
	sync_pool $pool
	log_must zpool freeze $pool

	[[ $1 == sa ]] && log_must setfacl -b $d/moved
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

	# had[] collects, between bars, the states each of these files has
	# from here on.
	for obj in promote promote-chmod; do
		log_must setfacl -m u:$ZFS_ACL_STAFF1:rwx $d/$obj
		fsync_path $d/$obj
		now=$(acl_state %a $d/$obj) ||
		    log_fail "xattr=$1: cannot read the state of $obj"
		had[$obj]="|$now|"
		if [[ $obj == promote-chmod ]]; then
			log_must chmod 600 $d/$obj
			now=$(acl_state %a $d/$obj) ||
			    log_fail "xattr=$1: cannot read the state of $obj"
			had[$obj]="${had[$obj]}$now|"
		fi
		log_must setfacl -b $d/$obj
		now=$(acl_state %a $d/$obj) ||
		    log_fail "xattr=$1: cannot read the state of $obj"
		[[ ${now%% *} == 600 ]] ||
		    log_fail "xattr=$1: $obj is ${now%% *} before the crash"
		had[$obj]="${had[$obj]}$now|"
	done

	# The mode that comes with an ACL is logged after it, so compare
	# the owner, the group and the ACL only.
	had[regroup]="|"
	for obj in "" "chgrp $ZFS_ACL_STAFF_GROUP" "setfacl -m g::rw-"; do
		[[ -z $obj ]] || log_must $obj $d/regroup
		now=$(acl_state %u:%g $d/regroup) ||
		    log_fail "xattr=$1: cannot read the state of regroup"
		had[regroup]="${had[regroup]}$now|"
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
		now=$(acl_state %a $d/$obj) || now=unreadable
		[[ "${had[$obj]}" == *"|$now|"* ]] ||
		    changed="$changed $obj(${now%% *}, not a state it had)"
	done
	now=$(acl_state %u:%g $d/regroup) || now=unreadable
	[[ "${had[regroup]}" == *"|$now|"* ]] ||
	    changed="$changed regroup(${now%% *}, not with this ACL)"
	log_must zpool destroy $pool
	log_must rm -f $vdir/live $vdir/crash
	[[ -z $changed ]] || log_fail "xattr=$1: replay changed$changed"
}

log_assert "Modes set by setfacl survive a crash with their ACLs"
log_onexit cleanup
require_saxattr_logging

vdir=$(mktemp -d $TEST_BASE_DIR/posix_007.XXXXXX) || log_fail "mktemp failed"
umask 022
test_crash dir
test_crash sa

log_pass "Modes set by setfacl survive a crash with their ACLs"
