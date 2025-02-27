#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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

log_pass "'zfs unmount -u' unloads keys for datasets as they are unmounted"
