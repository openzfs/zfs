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
# Copyright 2026 Ricardo Correia.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/getfsuuid/getfsuuid.kshlib

#
# DESCRIPTION:
# A "zpool reguid" changes the pool half of the filesystem UUID at the
# next mount of a filesystem; a mounted filesystem keeps its UUID until
# then.
#
# STRATEGY:
# 1. Create a filesystem and get its UUID.
# 2. Change the pool guid with "zpool reguid".
# 3. Verify that the mounted filesystem keeps its UUID.
# 4. Unmount and mount the filesystem.
# 5. Verify that the pool half of the UUID equals the new pool guid and
#    that the dataset half is unchanged.
#

verify_runnable "global"

typeset fs=$TESTPOOL/getfsuuid_reguid

function cleanup
{
	datasetexists $fs && destroy_dataset $fs
}

log_assert "zpool reguid changes the filesystem UUID at the next mount"
log_onexit cleanup

log_must zfs create $fs
typeset mnt=$(get_prop mountpoint $fs)
typeset old_guid=$(zpool get -Hp -o value guid $TESTPOOL)
typeset ds_guid=$(zfs get -Hp -o value guid $fs)

get_uuid $mnt
typeset old_uuid=$uuid
log_must test "$pool_half" = "$old_guid"
log_must test "$ds_half" = "$ds_guid"

log_must zpool reguid $TESTPOOL
typeset new_guid=$(zpool get -Hp -o value guid $TESTPOOL)
log_mustnot test "$new_guid" = "$old_guid"

# The mounted filesystem keeps its UUID
get_uuid $mnt
log_must test "$uuid" = "$old_uuid"

# The next mount shows the new pool guid
log_must zfs unmount $fs
log_must zfs mount $fs
get_uuid $mnt
log_must test "$pool_half" = "$new_guid"
log_must test "$ds_half" = "$ds_guid"

log_pass "zpool reguid changes the filesystem UUID at the next mount"
