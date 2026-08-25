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
# When set property exec=on on a filesystem, processes can be executed from
# this filesystem.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Copy '$STF_PATH/ls' to the ZFS file system.
# 3. Setting exec=on on this file system.
# 4. Make sure '$STF_PATH/ls' can work in this ZFS file system.
# 5. Make sure mmap which is using the PROT_EXEC calls succeed.
#

verify_runnable "both"

function cleanup
{
	log_must rm $TESTDIR/ls
}

log_assert "Setting exec=on on a filesystem, processes can be executed from " \
	"this file system."
log_onexit cleanup

log_must cp $STF_PATH/ls $TESTDIR/ls
log_must zfs set exec=on $TESTPOOL/$TESTFS
log_must $TESTDIR/ls
log_must mmap_exec $TESTDIR/ls

log_pass "Setting exec=on on filesystem testing passed."
