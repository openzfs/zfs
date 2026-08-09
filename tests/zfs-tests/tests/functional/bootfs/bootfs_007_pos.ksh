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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2015 Nexenta Systems, Inc.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Setting bootfs on a pool which was configured with the whole disk
# (i.e. EFI) works.
#
# STRATEGY:
# 1. create a pool with a whole disk
# 2. create a filesystem on this pool
# 3. verify we can set bootfs on the filesystem we just created.
#

verify_runnable "global"

function cleanup {
	if poolexists $TESTPOOL ; then
		destroy_pool "$TESTPOOL"
	fi
}

log_onexit cleanup

DISK=${DISKS%% *}
typeset EFI_BOOTFS=$TESTPOOL/efs
typeset assert_mesg="setting bootfs on a pool which was configured with the \
    whole disk works"

log_assert $assert_mesg
create_pool "$TESTPOOL" "$DISK"
log_must zfs create $EFI_BOOTFS

log_must zpool set bootfs=$EFI_BOOTFS $TESTPOOL

log_pass $assert_mesg
