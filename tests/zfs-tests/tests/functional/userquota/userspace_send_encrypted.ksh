#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2021, George Amanakis <gamanakis@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# Sending raw encrypted datasets back to the source dataset succeeds.
#
#
# STRATEGY:
# 1. Create encrypted source dataset, set userquota and write a file
# 2. Create base snapshot
# 3. Write new file, snapshot, get userspace
# 4. Raw send both snapshots
# 5. Destroy latest snapshot at source and rollback
# 6. Unmount, unload key from source
# 7. Raw send latest snapshot back to source
# 8. Mount both source and target datasets
# 9. Verify encrypted datasets support 'zfs userspace' and 'zfs groupspace'
#	and the accounting is done correctly
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/userspace_encrypted"
POOLNAME="testpool$$"
ENC_SOURCE="$POOLNAME/source"
ENC_TARGET="$POOLNAME/target"

log_assert "Sending raw encrypted datasets back to the source dataset succeeds."

# Setup pool and create source
truncate -s 200m $FILEDEV
log_must zpool create -o feature@encryption=enabled $POOLNAME \
	$FILEDEV
log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt " \
	"$ENC_SOURCE"

# Set user quota and write file
log_must zfs set userquota@$QUSER1=50m $ENC_SOURCE
mkmount_writable $ENC_SOURCE
mntpnt=$(get_prop mountpoint $ENC_SOURCE)
log_must user_run $QUSER1 mkfile 10m /$mntpnt/file1
sync

# Snapshot
log_must zfs snap $ENC_SOURCE@base

# Write new file, snapshot, get userspace
log_must user_run $QUSER1 mkfile 20m /$mntpnt/file2
log_must zfs snap $ENC_SOURCE@s1

# Raw send both snapshots
log_must eval "zfs send -w $ENC_SOURCE@base | zfs recv " \
	"$ENC_TARGET"
log_must eval "zfs send -w -i @base $ENC_SOURCE@s1 | zfs recv " \
	"$ENC_TARGET"

# Destroy latest snapshot at source and rollback
log_must zfs destroy $ENC_SOURCE@s1
log_must zfs rollback $ENC_SOURCE@base
rollback_uspace=$(zfs userspace -Hp $ENC_SOURCE | \
	awk "/$QUSER1/"' {printf "%d\n", $4 / 1024 / 1024}')

# Unmount, unload key
log_must zfs umount $ENC_SOURCE
log_must zfs unload-key -a

# Raw send latest snapshot back to source
log_must eval "zfs send -w -i @base $ENC_TARGET@s1 | zfs recv " \
	"$ENC_SOURCE"

#  Mount encrypted datasets and verify they support 'zfs userspace' and
# 'zfs groupspace' and the accounting is done correctly
log_must eval "echo 'password' | zfs load-key $ENC_SOURCE"
log_must eval "echo 'password' | zfs load-key $ENC_TARGET"
log_must zfs mount $ENC_SOURCE
log_must zfs mount $ENC_TARGET
sync

sleep 5

src_uspace=$(zfs userspace -Hp $ENC_SOURCE | \
	awk "/$QUSER1/"' {printf "%d\n", $4 / 1024 / 1024}')
tgt_uspace=$(zfs userspace -Hp $ENC_TARGET | \
	awk "/$QUSER1/"' {printf "%d\n", $4 / 1024 / 1024}')
log_must test "$src_uspace" -eq "$tgt_uspace"
log_must test "$rollback_uspace" -ne "$src_uspace"

src_uquota=$(zfs userspace -Hp $ENC_SOURCE | awk "/$QUSER1/"' {print $5}')
tgt_uquota=$(zfs userspace -Hp $ENC_TARGET | awk "/$QUSER1/"' {print $5}')
log_must test "$src_uquota" -eq "$tgt_uquota"

# Cleanup
cleanup

log_pass "Sending raw encrypted datasets back to the source dataset succeeds."
