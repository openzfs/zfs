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

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
#  system related filesystems can not be renamed or destroyed
#
# STRATEGY:
#
# 1) check if the current system is installed as zfs rootfs
# 2) get the rootfs
# 3) try to rename the rootfs to some newfs, which should fail.
# 4) try to destroy the rootfs, which should fail.
# 5) try to destroy the rootfs with -f which should fail
# 6) try to destroy the rootfs with -fR which should fail
#

verify_runnable "global"
log_assert "system related filesystems can not be renamed or destroyed"

typeset rootpool=$(get_rootpool)
typeset rootfs=$(get_rootfs)

log_mustnot zfs rename $rootfs $rootpool/newfs
log_mustnot zfs rename -f $rootfs $rootpool/newfs

log_mustnot zfs destroy $rootfs
log_mustnot zfs destroy -f $rootfs

log_pass "system related filesystems can not be renamed or destroyed"
