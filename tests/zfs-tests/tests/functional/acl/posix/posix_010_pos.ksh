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
#	With acltype=posix, xattr=dir and SA xattr logging, the access ACL
#	that a commit for another file writes is the one that was set.  An
#	ACL that replaces it while the commit is being written does not
#	take its place in the log: whole, ahead of a chgrp made in
#	between, or cut to the length of the first.
#
# STRATEGY:
#	On a pool of its own, for a second ACL of the same length as the
#	first and for a longer one:
#	1. Create a file with an ACL and sync.  Freeze the pool.
#	2. Change the file's ACL.
#	3. Delay the I/O of the pool's device.  A commit on a frozen
#	   pool waits for the TXG of each write it logs before its log
#	   block is written, so the commit of step 4 takes a while.
#	4. Start an fsync of an unrelated file.  While its commit is
#	   written, change the file's group and then its ACL again.  If
#	   the fsync has already returned, nothing would be tested: the
#	   test is unsupported.
#	5. Copy the pool's device, as a crash would leave it, and import
#	   the copy, which replays its log.
#	6. Verify that the file has a mode, a group and an ACL it had
#	   together.  The mask is the same in every ACL, so the mode
#	   does not change.
#

verify_runnable "global"

typeset pool=$TESTPOOL1
typeset vdir

function cleanup
{
	zinject -c all >/dev/null
	poolexists $pool && destroy_pool $pool
	[[ -n $vdir ]] && log_must rm -rf $vdir
}

# Print the mode, the owner, the group and the ACL.  Fail if any cannot
# be read: an unreadable ACL is not a state.
function acl_state # path
{
	typeset s a

	s=$(stat -c '%a %u:%g' $1) || return 1
	a=$(getfacl -cpn $1 2>/dev/null) || return 1
	echo "$s $a"
}

# fsync the first file and, as soon as that has started, give the second
# the group and then the access ACL of the third.  The fsync must still be
# running when that begins.
function fsync_and_change # other file template
{
	python3 -c "
import os, sys, time
other, path, template = sys.argv[1:4]
name = 'system.posix_acl_access'
acl = os.getxattr(template, name)
gid = os.stat(template).st_gid
fd = os.open(other, os.O_WRONLY)
os.pwrite(fd, b'o', 0)
r, w = os.pipe()
pid = os.fork()
if pid == 0:
    os.write(w, b'x')
    os.fsync(fd)
    os._exit(0)
os.read(r, 1)
time.sleep(0.05)
done, status = os.waitpid(pid, os.WNOHANG)
if done:
    sys.exit(2 if status == 0 else 1)
os.chown(path, -1, gid)
os.setxattr(path, name, acl)
sys.exit(os.WEXITSTATUS(os.waitpid(pid, 0)[1]))
" $1 $2 $3
	case $? in
	0)	;;
	2)	log_unsupported "The fsync returned before the change" ;;
	*)	log_fail "The fsync or the change failed" ;;
	esac
}

function test_crash # form second-acl
{
	typeset d=/$pool
	typeset first now had

	log_must truncate -s 256m $vdir/live
	log_must zpool create -O acltype=posix -O xattr=dir $pool $vdir/live
	require_saxattr_logging $pool
	# The first commit creates the log and waits for its TXG.
	log_must dd if=/dev/zero of=$d/seed bs=1 count=1 conv=fsync
	log_must touch $d/file $d/template $d/other
	log_must chmod 660 $d/file $d/template
	log_must setfacl -m u:$ZFS_ACL_STAFF1:r-- $d/file
	# The group and the ACL that step 4 gives the file.
	log_must chgrp $ZFS_ACL_STAFF_GROUP $d/template
	log_must setfacl -m $2 $d/template
	sync_pool $pool
	log_must zpool freeze $pool

	now=$(acl_state $d/file) || log_fail "$1: cannot read the state"
	had="|$now|"
	log_must setfacl -m u:$ZFS_ACL_STAFF1:-w- $d/file
	first=$(acl_state $d/file) || log_fail "$1: cannot read the state"
	had="$had$first|"
	log_must zinject -d $vdir/live -D 100:16 $pool

	fsync_and_change $d/other $d/file $d/template
	log_must zinject -c all
	now=$(acl_state $d/file) || log_fail "$1: cannot read the state"
	[[ "$now" == "$(acl_state $d/template)" ]] ||
	    log_fail "$1: the file is not as the template: $now"
	# Between the chgrp and the second ACL: the first one, regrouped.
	had="$had${now%% [!0-9: ]*} ${first#* * }|$now|"

	log_must cp $vdir/live $vdir/crash
	log_must zpool export $pool
	log_must zpool import -d $vdir/crash $pool

	now=$(acl_state $d/file) || now="an unreadable ACL"
	log_must zpool destroy $pool
	log_must rm -f $vdir/live $vdir/crash
	[[ "$had" == *"|$now|"* ]] ||
	    log_fail "$1: replay left a state the file never had: $now"
}

log_assert "A commit writes the ACL that was set, not the one replacing it"
log_onexit cleanup
require_saxattr_logging

vdir=$(mktemp -d $TEST_BASE_DIR/posix_010.XXXXXX) || log_fail "mktemp failed"
umask 022
test_crash "same length" u:$ZFS_ACL_STAFF1:rw-
test_crash "longer" u:$ZFS_ACL_STAFF1:rw-,u:root:r--

log_pass "A commit writes the ACL that was set, not the one replacing it"
