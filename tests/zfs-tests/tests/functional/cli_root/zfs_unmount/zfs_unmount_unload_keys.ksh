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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_unmount/zfs_unmount.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# "zfs unmount -u" should allow the user to unload their encryption
# keys while unmounting one or more datasets
#
# STRATEGY:
# 1. Create a hierarchy of encrypted datasets
# 2. Test that 'zfs unmount -u' unloads keys as it unmounts a dataset
# 3. Test that 'zfs unmount -u' unloads keys as it unmounts multiple datasets
# 4. Test that 'zfs unmount -u' returns an error if the key is still in
#    use by a clone.
# 5. Test that 'zfs unmount -u' unloads the key when a key-inheriting child's
#    mountpoint sorts before its encryption root's.
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
	datasetexists $TESTPOOL/$TESTFS2/newroot && \
		destroy_dataset $TESTPOOL/$TESTFS2/newroot -r
	datasetexists $TESTPOOL/$TESTFS2/child && \
		destroy_dataset $TESTPOOL/$TESTFS2/child -r

}
log_onexit cleanup

log_assert "'zfs unmount -u' should unload keys for datasets as they are unmounted"
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/$TESTFS2"
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/$TESTFS2/newroot"
log_must zfs create $TESTPOOL/$TESTFS2/child

log_must zfs umount -u $TESTPOOL/$TESTFS2/newroot
log_must key_unavailable $TESTPOOL/$TESTFS2/newroot
log_must eval "echo 'password' | zfs mount -l $TESTPOOL/$TESTFS2/newroot"

log_must zfs umount -u $TESTPOOL/$TESTFS2
log_must key_unavailable $TESTPOOL/$TESTFS2
log_must key_unavailable $TESTPOOL/$TESTFS2/newroot
log_must key_unavailable $TESTPOOL/$TESTFS2/child
log_must eval "echo 'password' | zfs mount -l $TESTPOOL/$TESTFS2/newroot"

log_must zfs snap $TESTPOOL/$TESTFS2/newroot@1
log_must zfs clone $TESTPOOL/$TESTFS2/newroot@1 $TESTPOOL/$TESTFS2/clone
log_mustnot zfs umount -u $TESTPOOL/$TESTFS2/newroot
log_must key_available $TESTPOOL/$TESTFS2/newroot
log_must mounted $TESTPOOL/$TESTFS2/newroot

# The changelist unmounts mountpoints in reverse alphabetical order, so
# z_root (which sorts after a_child) is unmounted first, before its
# key-inheriting child. The root comes down while the child still holds the
# shared key, so the key must unload only after the whole subtree is down.
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase -o mountpoint=/$TESTPOOL/z_root $TESTPOOL/$TESTFS2/encroot"
log_must zfs create -o mountpoint=/$TESTPOOL/a_child $TESTPOOL/$TESTFS2/encroot/child
log_must mounted $TESTPOOL/$TESTFS2/encroot
log_must mounted $TESTPOOL/$TESTFS2/encroot/child
log_must zfs umount -u $TESTPOOL/$TESTFS2/encroot
log_must key_unavailable $TESTPOOL/$TESTFS2/encroot
log_must unmounted $TESTPOOL/$TESTFS2/encroot
log_must unmounted $TESTPOOL/$TESTFS2/encroot/child

log_pass "'zfs unmount -u' unloads keys for datasets as they are unmounted"
