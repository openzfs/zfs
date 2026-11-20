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
# illumos ZFS doesn't complete the user accounting of an encrypted filesystem
# that it receives from a stream that isn't raw. Such a filesystem, whose
# local MAC doesn't cover the project accounting, keeps that form when the
# projectmac pool property is mixed.
#
# STRATEGY:
# 1. Import a pool made by illumos ZFS, holding an encrypted filesystem and a
#    child that was received into it from a stream that isn't raw, then
#    mounted and written, and check that the projectmac pool property is mixed
# 2. Load the key and check that the received filesystem has the illumos form
# 3. Mount it, wait for the accounting upgrade that mounting starts, write to
#    it, and check that it kept its form
# 4. Export, import and mount again, write, and check that the form was kept
#

verify_runnable "global"

POOL_NAME=recvmac
POOL_FILE=illumos_local_mac_received.dat
FS=$POOL_NAME/enc
RECV=$FS/recv

function cleanup
{
	poolexists $POOL_NAME && log_must zpool destroy $POOL_NAME
	log_must rm -f /$TESTPOOL/$POOL_FILE
}
log_onexit cleanup

log_assert "A filesystem that illumos ZFS received keeps its local MAC"

BLOCKFILES=$STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles
log_must eval "bzcat $BLOCKFILES/$POOL_FILE.bz2 > /$TESTPOOL/$POOL_FILE"

# 1.
log_must zpool import -d /$TESTPOOL $POOL_NAME
check_pool_projectmac $POOL_NAME mixed default

# 2.
log_must eval "echo password | zfs load-key $FS"
check_projectmac $RECV off

# 3.
log_must zfs mount $FS
log_must zfs mount $RECV
mntpnt=$(get_prop mountpoint $RECV)
log_must grep -q "illumos received file" $mntpnt/testfile
typeset -i tries=0
while ! zfs userspace -H $RECV 2>/dev/null | grep -q .; do
	((tries++ < 30)) || log_fail "user accounting of $RECV didn't complete"
	sleep 1
done
sync_pool $POOL_NAME
write_and_sync $RECV
check_projectmac $RECV off
check_pool_projectmac $POOL_NAME mixed default

# 4.
log_must zpool export $POOL_NAME
log_must zpool import -d /$TESTPOOL $POOL_NAME
load_and_mount $FS
log_must zfs mount $RECV
write_and_sync $RECV
check_projectmac $RECV off
log_must grep -q "written after receive" $mntpnt/file2

log_pass "A filesystem that illumos ZFS received keeps its local MAC"
