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
# DESCRIPTION:
#	A write by another user with write access clears S_ISUID and S_ISGID,
#	and they stay cleared when the owner sets an ACL while that write is
#	still in progress.
#
# STRATEGY:
#	1. Create a setuid/setgid file (6775) of 32 MiB plus one 128k record,
#	   that another user can write through an extended ACL entry
#	   (u:USER:rwx).
#	2. Export and import the pool, and delay reads from its disk.
#	   zfs_write() splits a write into transactions of at most
#	   DMU_MAX_ACCESS / 2 (32 MiB), so a write of 32 MiB plus one byte at
#	   offset 0 clears the set-id bits in its first transaction (full
#	   records, no reads), then waits on the read of the last record for
#	   its one-byte second transaction before it finishes.
#	3. Start that write as the other user.  Once the file's mtime shows
#	   that the first transaction (and so the clear) has happened, and
#	   while the write is still in progress, the owner sets an ACL without
#	   execute bits (u::rw-,g::rw-,m::rw-,o::r--).  zfs_write() clears the
#	   set-id bits only while an execute bit is set, so its second
#	   transaction leaves the mode setfacl stored as it is.
#	4. Verify that after the write completes the mode is exactly 0664 and
#	   the ACL is the one the owner set, and the same after export and
#	   import.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "global"
log_assert "Set-id bits cleared by a write stay cleared when setfacl" \
	"runs during the write"

typeset file=$TESTDIR/posix-setid-write
typeset user=$ZFS_ACL_STAFF1
typeset -i rec=131072
typeset -i chunk=$((32 * 1024 * 1024))
typeset -i delay_ms=5000
typeset wpid=""
typeset disk=${DISKS%% *}

function cleanup
{
	zinject -c all >/dev/null 2>&1
	if [[ -n $wpid ]]; then
		kill $wpid 2>/dev/null
		wait $wpid 2>/dev/null
	fi
	poolexists $TESTPOOL || zpool import $TESTPOOL
	rm -f $file
	zfs inherit primarycache $TESTPOOL/$TESTFS
	zfs inherit recordsize $TESTPOOL/$TESTFS
}

log_onexit cleanup

function check_state # when
{
	typeset when=$1
	typeset mode=$(stat -c %a $file)
	typeset acl=$(getfacl -cpn $file | tr '\n' ' ')

	log_note "$when: mode $mode, ACL: $acl"
	[[ $mode == "664" ]] || log_fail "$when: mode is $mode, expected" \
	    "664: set-id bits are set after a completed write by $user"
	[[ $acl == *"user::rw- "* && $acl == *"user:$(id -u $user):rwx"* &&
	    $acl == *"mask::rw- "* && $acl == *"other::r-- "* ]] ||
	    log_fail "$when: ACL is not the one the owner set: $acl"
}

# Data records are never cached, so the write's read of record 1 goes to disk.
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
log_must zfs set primarycache=metadata $TESTPOOL/$TESTFS

log_must dd if=/dev/urandom of=$file bs=$rec count=$((chunk / rec + 1))
log_must chmod 6775 $file
log_must setfacl -m u:$user:rwx $file
log_must eval "[[ $(stat -c %a $file) == 6775 ]]"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# Load the file's metadata and its cached ACL before reads are delayed.
log_must dd if=$file of=/dev/null bs=1 count=1
log_must getfacl $file
typeset mtime0=$(stat -c %y $file)

log_must zinject -d $disk -D $delay_ms:1 -T read $TESTPOOL

typeset -i start=$SECONDS
user_run $user dd if=/dev/zero of=$file bs=$((chunk + 1)) count=1 \
	conv=notrunc &
wpid=$!

# zfs_write() clears the set-id bits and sets the mtime in the same
# transaction, the first one of this write.  The second one waits on the
# delayed read.
typeset -i i=0
while [[ $(stat -c %y $file) == "$mtime0" ]]; do
	kill -0 $wpid 2>/dev/null || break
	((i++ < 100)) || break
	sleep 0.05
done
[[ $(stat -c %y $file) != "$mtime0" ]] ||
	log_fail "The write by $user did not update the mtime"
kill -0 $wpid 2>/dev/null ||
	log_fail "The write by $user finished before setfacl could run"

log_must setfacl -m u::rw-,g::rw-,m::rw-,o::r-- $file

wait $wpid
typeset -i rv=$?
wpid=""
typeset -i elapsed=$((SECONDS - start))
log_note "write by $user: exit $rv after ${elapsed}s"
((rv == 0)) || log_fail "The write by $user failed: $rv"
((elapsed >= delay_ms / 1000 - 1)) ||
	log_fail "The write by $user was not delayed (${elapsed}s)"
log_must zinject -c all

check_state "After the write"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
check_state "After export and import"

log_pass "Set-id bits cleared by a write stay cleared when setfacl" \
	"runs during the write"
