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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify acl after upgrading.
# STRATEGY:
#	1. Create a low version fs.
#	2. Create a directory and chmod it.
#	3. Upgrade the fs.
#	4. Create a file under the directory and list it.
#

verify_runnable "both"

function acl_upgrade_cleannup
{
	log_must zfs destroy -rR $ACL_UPGRADE_FS
}

log_assert "Verify acl after upgrading."
log_onexit acl_upgrade_cleannup

ACL_UPGRADE_FS=$TESTPOOL/acl_upgrade_fs.$$

log_must zfs create -o version=2 $ACL_UPGRADE_FS
mntpnt=$(get_prop mountpoint $ACL_UPGRADE_FS)
log_must mkdir $mntpnt/dir.$$
log_must chmod A+owner@:rwxp:f:allow,group@:rwxp:f:allow $mntpnt/dir.$$
log_must zfs upgrade $ACL_UPGRADE_FS
log_must touch $mntpnt/dir.$$/file.$$
log_must eval "ls -V $mntpnt/dir.$$/file.$$ > /dev/null 2>&1"

log_pass "Verify acl after upgrading."

