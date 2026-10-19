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
#	With sync=always and failmode=continue, create, mkdir and symlink
#	return an error when the pool suspends during their ZIL commit.
#	The new entry stays in place: it must be visible once the pool
#	resumes, and no reference on its inode may be leaked.
#
# STRATEGY:
#	1. Create a pool with a separate log device and failmode=continue,
#	   and a dataset with sync=always.
#	2. For each of create, mkdir and symlink: inject write errors on
#	   both devices, expect the operation to fail, wait for the pool
#	   to suspend, then clear the errors and resume the pool.
#	3. Verify each entry is visible without dropping the dentry cache.
#	4. Remove the entries and verify their objects are freed; a leaked
#	   inode reference keeps them on the delete queue.
#	5. Verify the pool exports, which a leaked inode prevents.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/failmode/failmode.kshlib

verify_runnable "global"

typeset desc="create/mkdir/symlink failing in zil_commit do not leak the inode"

log_assert $desc
log_onexit failmode_sync_cleanup

read -r DISK1 DISK2 _ <<<"$DISKS"
typeset mntpnt=$TESTDIR
typeset ops="create mkdir symlink"

log_must zpool create -f -o failmode=continue $TESTPOOL $DISK1 log $DISK2
log_must zfs create -o mountpoint=$mntpnt -o sync=always \
    -o logbias=latency $TESTPOOL/$TESTFS

# Create the ZIL header now, which waits for a full txg sync.
log_must dd if=/dev/zero of=$mntpnt/base bs=128k count=1 conv=fsync
log_must zpool sync

function object_listed # obj
{
	typeset obj=$1
	typeset out

	out=$(zdb -dd $TESTPOOL/$TESTFS $obj 2>&1)
	if grep -qE "^ +$obj +" <<<"$out" ; then
		return 0
	elif grep -q "dmu_object_info() failed, errno 2" <<<"$out" ; then
		return 1
	fi
	log_fail "zdb failed on object $obj: $out"
}

for op in $ops ; do
	log_must zpool sync
	log_must zinject -d $DISK1 -e io -T write $TESTPOOL
	log_must zinject -d $DISK1 -e nxio -T probe $TESTPOOL
	log_must zinject -d $DISK2 -e io -T write $TESTPOOL
	log_must zinject -d $DISK2 -e nxio -T probe $TESTPOOL

	# A single open(O_CREAT), as touch retries a failed create.
	case $op in
	create)		( : > $mntpnt/$op ) & ;;
	mkdir)		mkdir $mntpnt/$op & ;;
	symlink)	ln -s base $mntpnt/$op & ;;
	esac
	typeset -i pid=$!

	typeset -i tries=10
	until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
		if ((tries-- == 0)); then
			log_fail "$op: pool didn't suspend"
		fi
		sleep 1
	done

	# With failmode=continue, the operation must return, not block.
	typeset -i blocked=1
	for i in {1..10} ; do
		if ! kill -0 $pid 2>/dev/null ; then
			blocked=0
			break
		fi
		sleep 1
	done

	log_must zinject -c all
	log_must zpool clear $TESTPOOL

	wait $pid
	typeset -i rc=$?
	log_note "$op: blocked=$blocked rc=$rc"
	log_must test $blocked -eq 0
	log_must test $rc -ne 0
done

typeset -i failed=0
for op in $ops ; do
	if [[ ! -e $mntpnt/$op && ! -L $mntpnt/$op ]] ; then
		log_note "$op: entry hidden by a stale dentry"
		failed=1
	fi
done

# Drop the dentries so that the leak check below runs in any case.
log_must eval "echo 2 > /proc/sys/vm/drop_caches"

log_must zpool sync
typeset -A objs
for op in $ops ; do
	objs[$op]=$(stat -c %i $mntpnt/$op) || log_fail "$op: entry missing"
	object_listed ${objs[$op]} || log_fail "$op: object not on disk"
done
log_must rm $mntpnt/create $mntpnt/symlink
log_must rmdir $mntpnt/mkdir

for op in $ops ; do
	typeset obj=${objs[$op]}
	typeset -i tries=10
	while object_listed $obj ; do
		if ((tries-- == 0)); then
			log_note "$op: object $obj not freed, inode leaked"
			failed=1
			break
		fi
		log_must zpool sync
		sleep 1
	done
done

if ((failed)); then
	log_fail "$desc"
fi

log_must zpool export $TESTPOOL

log_pass $desc
