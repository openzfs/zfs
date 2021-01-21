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
# Copyright 2020, George Amanakis <gamanakis@gmail.com>. All rights reserved.
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
# 2. Create base and an additional snapshot (s1)
# 3. Unmount the source dataset
# 4. Raw send the base snapshot to a new target dataset
# 5. Raw send incrementally the s1 snapshot to the new target dataset
# 6. Mount both source and target datasets
# 7. Verify encrypted datasets support 'zfs userspace' and 'zfs groupspace'
#	and the accounting is done correctly
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

function log_must_unsupported
{
	log_must_retry "unsupported" 3 "$@"
	(( $? != 0 )) && log_fail
}

log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/userspace_encrypted"
POOLNAME="testpool$$"
ENC_SOURCE="$POOLNAME/source"
ENC_TARGET="$POOLNAME/target"

log_assert "Sending raw encrypted datasets back to the source dataset succeeds."

# Setup
truncate -s 200m $FILEDEV
log_must zpool create -o feature@encryption=enabled $POOLNAME \
	$FILEDEV

# Create encrypted source dataset
log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt " \
	"$ENC_SOURCE"

# Set user quota and write file
log_must zfs set userquota@$QUSER1=50m $ENC_SOURCE
mkmount_writable $ENC_SOURCE
mntpnt=$(get_prop mountpoint $ENC_SOURCE)
log_must user_run $QUSER1 mkfile 20m /$mntpnt/file
sync

# Snapshot, raw send to new dataset
log_must zfs snap $ENC_SOURCE@base
log_must zfs snap $ENC_SOURCE@s1
log_must zfs umount $ENC_SOURCE
log_must eval "zfs send -w $ENC_SOURCE@base | zfs recv " \
	"$ENC_TARGET"

log_must eval "zfs send -w -i @base $ENC_SOURCE@s1 | zfs recv " \
	"$ENC_TARGET"

log_must zfs destroy $ENC_SOURCE@s1
log_must eval "zfs send -w -i @base $ENC_TARGET@s1 | zfs recv " \
	"$ENC_SOURCE"

#  Mount encrypted datasets and verify they support 'zfs userspace' and
# 'zfs groupspace' and the accounting is done correctly
log_must zfs mount $ENC_SOURCE
log_must eval "echo password | zfs load-key $ENC_TARGET"
log_must zfs mount $ENC_TARGET
sync

src_uspace=$(( $(zfs userspace -Hp $ENC_SOURCE | grep $QUSER1 | \
	awk '{print $4}')/1024/1024))
tgt_uspace=$(( $(zfs userspace -Hp $ENC_TARGET | grep $QUSER1 | \
	awk '{print $4}')/1024/1024))
log_must test "$src_uspace" -eq "$tgt_uspace"

src_uquota=$(zfs userspace -Hp $ENC_SOURCE | grep $QUSER1 | awk '{print $5}')
tgt_uquota=$(zfs userspace -Hp $ENC_TARGET | grep $QUSER1 | awk '{print $5}')
log_must test "$src_uquota" -eq "$tgt_uquota"

# Cleanup
cleanup

log_pass "Sending raw encrypted datasets back to the source dataset succeeds."
