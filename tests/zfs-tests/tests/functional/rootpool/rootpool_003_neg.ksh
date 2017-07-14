#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
