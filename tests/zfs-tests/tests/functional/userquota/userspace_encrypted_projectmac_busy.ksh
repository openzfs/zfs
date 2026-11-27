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

. $STF_SUITE/tests/functional/userquota/projectmac.kshlib

#
# DESCRIPTION:
# Setting the projectmac pool property while encrypted filesystems are being
# written and snapshotted converts them all, without errors, though some are
# dirty or snapshotted in the transaction that converts the others.
#
# STRATEGY:
# 1. Create an encrypted filesystem, and start writing to it and taking
#    snapshots of it in the background
# 2. Meanwhile set projectmac to the other form, back, and to the other again
# 3. Stop the writers, then check that the filesystem has the form that was
#    set last, and that every snapshot has one form or the other and can be
#    sent
# 4. Scrub the pool, then export and import it, and check that the form and
#    the file contents were kept
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_encrypted_projectmac_busy
SUMS=$TEST_BASE_DIR/userspace_encrypted_projectmac_busy.sums
STOP=$TEST_BASE_DIR/userspace_encrypted_projectmac_busy.stop
FS=$POOLNAME/fs

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV $SUMS $STOP
}

function writer # <file>
{
	typeset -i n=0

	while [[ ! -e $STOP ]]; do
		if ! dd if=/dev/urandom of=$1.$(( n % 4 )) bs=64k count=4 \
		    2>/dev/null; then
			log_note "writing $1.$(( n % 4 )) failed"
			return 1
		fi
		(( n += 1 ))
	done
}

function snapshotter
{
	typeset -i n=0

	while [[ ! -e $STOP ]]; do
		if ! zfs snapshot $FS@s$n; then
			log_note "zfs snapshot $FS@s$n failed"
			return 1
		fi
		sleep 0.2
		(( n += 1 ))
	done
}

log_onexit cleanup

log_assert "Setting projectmac converts datasets that are being written"

truncate -s 512m $FILEDEV
log_must zpool create $POOLNAME $FILEDEV
form=$(projectmac_default $POOLNAME)
other=$(projectmac_other $form)

# 1.
create_encrypted $FS
mntpnt=$(get_prop mountpoint $FS)
pids=""
for i in 1 2 3 4; do
	writer $mntpnt/f$i &
	pids="$pids $!"
done
snapshotter &
pids="$pids $!"

# 2.
for value in $other $form $other; do
	log_must zpool set projectmac=$value $POOLNAME
	sleep 1
done

# 3.
log_must touch $STOP
for pid in $pids; do
	log_must wait $pid
done
sync_pool $POOLNAME
check_projectmac $FS $other
write_and_sync $FS
check_projectmac $FS $other
for snap in $(zfs list -H -o name -t snapshot -r $FS); do
	value=$(get_prop projectmac $snap)
	[[ $value == on || $value == off ]] ||
	    log_fail "$snap has projectmac=$value"
	log_must eval "zfs send $snap > /dev/null"
done
log_must eval "cksum $mntpnt/f* > $SUMS"

# 4.
log_must zpool scrub $POOLNAME
wait_scrubbed $POOLNAME
log_must check_pool_status $POOLNAME "errors" "No known data errors"
log_must zpool export $POOLNAME
log_must zpool import -d $TEST_BASE_DIR $POOLNAME
load_and_mount $FS
check_projectmac $FS $other
log_must eval "cksum $mntpnt/f* | cmp -s - $SUMS"
log_must zfs projectspace $FS

log_pass "Setting projectmac converts datasets that are being written"
