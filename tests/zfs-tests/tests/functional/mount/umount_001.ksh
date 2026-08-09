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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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
	log_must zfs umount $TESTPOOL/$TESTFS.$fs
	log_must unmounted $TESTDIR.$fs
	log_must zfs mount $TESTPOOL/$TESTFS.$fs
	log_must mounted $TESTDIR.$fs
done

log_pass "All file systems are unmounted"
