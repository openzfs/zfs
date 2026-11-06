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
# An encrypted filesystem written by illumos ZFS, whose local MAC doesn't cover
# the project accounting, can be mounted and keeps that form of local MAC.
# The projectmac pool property stays mixed until it is set, and new datasets
# take the platform default form.
#
# STRATEGY:
# 1. Import a pool made by illumos ZFS with project_quota active, holding an
#    encrypted filesystem and a snapshot of it, and check that the projectmac
#    pool property is mixed
# 2. Load the key, mount the illumos filesystem and read the snapshot
# 3. Write to the illumos filesystem and check that it kept its form of local
#    MAC and that the pool property is still mixed
# 4. Create and write a child of the illumos filesystem and a new encryption
#    root, and check that both have the platform default form, and that
#    nothing set the pool property
# 5. Set the pool property to the illumos form, and check that both new
#    datasets were converted to it at once
# 6. Export, import and mount again, and check that the forms were kept
#

verify_runnable "global"

POOL_NAME=noprojmac
POOL_FILE=illumos_local_mac_no_project.dat
FS=$POOL_NAME/testfs
CHILD=$FS/child
NEWFS=$POOL_NAME/newfs

function cleanup
{
	poolexists $POOL_NAME && log_must zpool destroy $POOL_NAME
	log_must rm -f /$TESTPOOL/$POOL_FILE
}
log_onexit cleanup

log_assert "Pools with illumos ZFS's local MAC can be mounted and keep it"

BLOCKFILES=$STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles
log_must eval "bzcat $BLOCKFILES/$POOL_FILE.bz2 > /$TESTPOOL/$POOL_FILE"

# 1.
log_must zpool import -d /$TESTPOOL $POOL_NAME
check_pool_projectmac $POOL_NAME mixed default

# 2.
load_and_mount $FS
mntpnt=$(get_prop mountpoint $FS)
log_must grep -q "illumos local MAC test file" $mntpnt/testfile
log_must eval "zfs send $FS@snap1 > /dev/null"

# 3.
write_and_sync $FS
sync_pool $POOL_NAME
check_projectmac $FS off
check_projectmac $FS@snap1 off
check_pool_projectmac $POOL_NAME mixed default

# 4.
log_must zfs create $CHILD
log_must eval "echo password | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o keylocation=prompt $NEWFS"
for fs in $CHILD $NEWFS; do
	write_and_sync $fs
done
sync_pool $POOL_NAME
for fs in $CHILD $NEWFS; do
	check_projectmac $fs on
done
check_pool_projectmac $POOL_NAME mixed default
log_mustnot eval "zpool history -i $POOL_NAME | grep -q projectmac="

# 5.
log_must zpool set projectmac=off $POOL_NAME
check_pool_projectmac $POOL_NAME off local
log_must eval "zpool history -i $POOL_NAME | grep -q 'projectmac=off'"
for fs in $CHILD $NEWFS; do
	check_projectmac $fs off
done

# 6.
log_must zpool export $POOL_NAME
log_must zpool import -d /$TESTPOOL $POOL_NAME
check_pool_projectmac $POOL_NAME off local
load_and_mount $FS
load_and_mount $NEWFS
for fs in $FS $CHILD $NEWFS; do
	write_and_sync $fs
done
sync_pool $POOL_NAME
for fs in $FS $CHILD $NEWFS; do
	check_projectmac $fs off
done
log_must eval "zfs send $FS@snap1 > /dev/null"

log_pass "Pools with illumos ZFS's local MAC can be mounted and keep it"
