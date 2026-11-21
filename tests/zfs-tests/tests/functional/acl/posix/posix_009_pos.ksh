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
#	With acltype=posix, a setfacl that cannot store an ACL, or cannot
#	set the mode that comes with it, leaves the mode and the ACL of the
#	file as they were.
#
# STRATEGY:
#	1. Create a file system with xattr=dir, and put a user over their
#	   quota in it.
#	2. As that user, add an entry to the ACL of a 0600 file of theirs.
#	   The xattr object for the ACL cannot be created, so setfacl fails.
#	3. Verify that the mode, the owner and the ACL of the file are
#	   what they were.
#	4. Remove the quota and verify that the same setfacl succeeds: the
#	   quota is what failed it.
#	5. With xattr=dir and with xattr=sa, on a 0600 file without an ACL
#	   and on a 0640 file with one, give a user's entry rwx again and
#	   again while another process sets and clears the file's immutable
#	   flag.  The flag refuses a setfacl outright, or, set after the
#	   ACL is stored, refuses the mode that goes with it.  Verify after
#	   every setfacl that failed that the mode and the ACL are what
#	   they were.
#

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS/quota
typeset dir=$TESTDIR/quota

typeset tpid=""

function cleanup
{
	stop_toggle
	[[ -e $dir/late ]] && chattr -i $dir/late
	datasetexists $fs && destroy_dataset $fs
}

# Set and clear the immutable flag of a file until killed, or until the
# test that started it is gone.
function toggle_immutable # path
{
	exec python3 -c "
import fcntl, os, platform, re, struct, sys
# _IOW('f', 2, long); the kernel reads an int.
w = 0x80000000 if re.match('ppc|mips|sparc|alpha', platform.machine()) \\
    else 0x40000000
FS_IOC_SETFLAGS = w | struct.calcsize('l') << 16 | 0x6602
FS_IMMUTABLE_FL = 0x10
fd = os.open(sys.argv[1], os.O_RDONLY)
parent = os.getppid()
while os.getppid() == parent:
    for flags in (FS_IMMUTABLE_FL, 0):
        try:
            fcntl.ioctl(fd, FS_IOC_SETFLAGS, struct.pack('i', flags))
        except OSError:
            pass
" $1
}

function stop_toggle
{
	if [[ -n $tpid ]]; then
		kill $tpid 2>/dev/null
		wait $tpid 2>/dev/null
		tpid=""
	fi
}

# Print the mode, the owner and the ACL.  Fail if any cannot be read.
function acl_state # path
{
	typeset s a

	s=$(stat -c '%a %U:%G' $1) || return 1
	a=$(getfacl -cpn $1 2>/dev/null) || return 1
	echo "$s $a"
}

log_assert "A setfacl that cannot add an ACL leaves mode and ACL as they were"
log_onexit cleanup

log_must zfs create -o xattr=dir $fs
log_must chmod 777 $dir
log_must user_run $ZFS_ACL_STAFF1 dd if=/dev/urandom of=$dir/big bs=1M count=8
log_must user_run $ZFS_ACL_STAFF1 touch $dir/file
log_must user_run $ZFS_ACL_STAFF1 chmod 600 $dir/file
log_must zfs set userquota@$ZFS_ACL_STAFF1=1M $fs
sync_pool $TESTPOOL

typeset before after

before=$(acl_state $dir/file) || log_fail "Cannot read the state of $dir/file"
[[ ${before%% *} == 600 ]] || log_fail "$dir/file is ${before%% *}"
log_mustnot user_run $ZFS_ACL_STAFF1 setfacl -m u:root:rwx $dir/file
after=$(acl_state $dir/file) ||
    log_fail "Cannot read the state of $dir/file after the failed setfacl"
[[ "$after" == "$before" ]] ||
    log_fail "The failed setfacl left the mode at ${after%% *}: $after"

log_must zfs set userquota@$ZFS_ACL_STAFF1=none $fs
log_must user_run $ZFS_ACL_STAFF1 setfacl -m u:root:rwx $dir/file
after=$(acl_state $dir/file) ||
    log_fail "Cannot read the state of $dir/file after setfacl"
[[ ${after%% *} == 670 && "$after" == *user:0:rwx* ]] ||
    log_fail "Without the quota, setfacl left: $after"

# Put the file in its start state.  The flag may refuse that for a while.
function late_reset # path start
{
	typeset -i n=0

	until setfacl -b $1 2>/dev/null && chmod 600 $1 2>/dev/null &&
	    { [[ $2 == none ]] ||
	    setfacl -m u:$ZFS_ACL_STAFF1:r-- $1 2>/dev/null; }; do
		((n += 1))
		((n < 10000)) || log_fail "Cannot reset $1"
	done
}

typeset x start
typeset -i failed tries
for x in dir sa; do
	log_must zfs set xattr=$x $fs
	for start in none older; do
		log_must rm -f $dir/late
		log_must touch $dir/late
		late_reset $dir/late $start
		before=$(acl_state $dir/late) ||
		    log_fail "Cannot read the state of $dir/late"

		toggle_immutable $dir/late &
		tpid=$!
		failed=0
		tries=0
		while ((failed < 100 && tries < 2000)); do
			((tries += 1))
			if setfacl -m u:$ZFS_ACL_STAFF1:rwx $dir/late \
			    2>/dev/null; then
				late_reset $dir/late $start
				continue
			fi
			((failed += 1))
			after=$(acl_state $dir/late) || log_fail "Cannot" \
			    "read the state of $dir/late after a failed" \
			    "setfacl"
			[[ "$after" == "$before" ]] || log_fail \
			    "xattr=$x, ACL before: $start: a setfacl that" \
			    "failed on a file made immutable meanwhile" \
			    "left: $after"
		done
		stop_toggle
		log_must chattr -i $dir/late
		log_note "xattr=$x, ACL before: $start:" \
		    "$failed of $tries setfacl failed"
		((failed > 0)) ||
		    log_fail "xattr=$x, ACL before: $start: no setfacl failed"
	done
done

log_pass "A setfacl that cannot add an ACL leaves mode and ACL as they were"
