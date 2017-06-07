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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zfs mount and unmount commands should mount and unmount existing
# file systems.
#
# STRATEGY:
# 1. Call zfs mount command
# 2. Make sure the file systems were mounted
# 3. Call zfs unmount command
# 4. Make sure the file systems were unmounted
#

for fs in 1 2 3; do
	log_must mounted $TESTDIR.$fs
	log_must $ZFS umount $TESTPOOL/$TESTFS.$fs
	log_must unmounted $TESTDIR.$fs
	log_must $ZFS mount $TESTPOOL/$TESTFS.$fs
	log_must mounted $TESTDIR.$fs
done

log_pass "All file systems are unmounted"
