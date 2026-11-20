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
# The projectmac pool property sets the form of encrypted filesystems' local
# MAC, with or without project accounting, and the projectmac dataset property
# shows it.
#
# STRATEGY:
# 1. Create a pool and check that projectmac is set to the platform default,
#    then create an encrypted filesystem, and check that it has that form and
#    didn't change projectmac
# 2. Set projectmac=mixed and create a second encrypted filesystem, and check
#    that it has the platform default form and didn't set projectmac; then
#    set projectmac=off, and create a snapshot of the first filesystem, an
#    unencrypted filesystem and an encrypted volume
# 3. Check that the encrypted filesystems have the form off, and that the
#    unencrypted filesystem and the volume have no form
# 4. Unload the second filesystem's key and check that it shows no form
# 5. Unmount the first filesystem, keeping its key loaded, then set
#    projectmac=on and check that it was converted, but not its snapshot
# 6. Load the second filesystem's key and check that it was converted by its
#    first write
# 7. Export and import the pool, and check that the forms were kept and that
#    the accounting works
# 8. Set projectmac to the form that isn't the platform default and then to
#    mixed, and check that the first filesystem keeps that form, that a new
#    child of it and a new encryption root take the platform default, and
#    that none of them sets projectmac
#
# Each check also gets the property from a channel program, which sees nil
# where zfs get shows "-".
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_encrypted_local_mac_project
FS1=$POOLNAME/fs1
FS2=$POOLNAME/fs2
FS3=$POOLNAME/fs3
VOL=$FS1/vol
CHILD=$FS1/child
PLAIN=$POOLNAME/plain

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

log_assert "The projectmac pool property sets the form of the local MAC"

truncate -s $SPA_MINDEVSIZE $FILEDEV
log_must zpool create $POOLNAME $FILEDEV

# 1.
form=$(projectmac_default $POOLNAME)
other=$(projectmac_other $form)
check_pool_projectmac $POOLNAME $form local
create_encrypted $FS1
check_projectmac $FS1 $form
check_pool_projectmac $POOLNAME $form local

# 2.
log_must zpool set projectmac=mixed $POOLNAME
create_encrypted $FS2
check_projectmac $FS2 $form
check_pool_projectmac $POOLNAME mixed default
log_must zpool set projectmac=off $POOLNAME
log_must zfs snapshot $FS1@snap
log_must zfs create $PLAIN
log_must zfs create -s -V 16m $VOL

# 3.
check_pool_projectmac $POOLNAME off local
check_projectmac $FS1 off
check_projectmac $FS1@snap off
check_projectmac $FS2 off
check_projectmac $PLAIN -
check_projectmac $VOL -

# 4.
log_must zfs unmount $FS2
log_must zfs unload-key $FS2
check_projectmac $FS2 -

# 5.
log_must zfs unmount $FS1
log_must zpool set projectmac=on $POOLNAME
check_projectmac $FS1 on
check_projectmac $FS1@snap off

# 6.
load_and_mount $FS2
check_projectmac $FS2 off
write_and_sync $FS2
check_projectmac $FS2 on

# 7.
log_must zpool export $POOLNAME
log_must zpool import -d $TEST_BASE_DIR $POOLNAME
check_pool_projectmac $POOLNAME on local
for fs in $FS1 $FS2; do
	load_and_mount $fs
	write_and_sync $fs
	check_projectmac $fs on
	log_must zfs userspace $fs
	log_must zfs projectspace $fs
done
check_projectmac $FS1@snap off
log_must eval "zfs send $FS1@snap > /dev/null"

# 8.
log_must zpool set projectmac=$other $POOLNAME
log_must zpool set projectmac=mixed $POOLNAME
check_pool_projectmac $POOLNAME mixed default
write_and_sync $FS1
check_projectmac $FS1 $other
log_must zfs create $CHILD
write_and_sync $CHILD
check_projectmac $CHILD $form
create_encrypted $FS3
check_projectmac $FS3 $form
check_pool_projectmac $POOLNAME mixed default

log_pass "The projectmac pool property sets the form of the local MAC"
