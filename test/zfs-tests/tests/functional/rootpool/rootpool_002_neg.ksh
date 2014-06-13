#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# the zfs rootpool can not be destroyed
#
# STRATEGY:
# 1) check if the current system is installed as zfs root
# 2) get the rootpool
# 3) try to destroy the rootpool, which should fail.
# 4) try to destroy the rootpool filesystem, which should fail.
#

verify_runnable "global"
log_assert "zpool/zfs destory <rootpool> should return error"

typeset rootpool=$(get_rootpool)
typeset tmpfile="/tmp/mounted-datasets.$$"

# Collect the currently mounted ZFS filesystems, so that we can repair any
# damage done by the attempted pool destroy. The destroy itself should fail, but
# some filesystems can become unmounted in the process, and aren't automatically
# remounted.
$MOUNT -p | $AWK '{if ($4 == "zfs") print $1" "$3}' > $tmpfile

log_mustnot $ZPOOL destroy $rootpool

# Remount any filesystems that the destroy attempt unmounted.
while read ds mntpt; do
	mounted $ds || log_must $MOUNT -Fzfs $ds $mntpt
done < $tmpfile
$RM -f $tmpfile

log_mustnot $ZFS destroy $rootpool

log_pass "rootpool can not be destroyed"
