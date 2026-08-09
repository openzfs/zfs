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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.cfg

#
# umount the ufs|ext fs if there is timedout in the ufs|ext test
#

if ismounted $FS_MNTPOINT $NEWFS_DEFAULT_FS ; then
	log_must umount -f $FS_MNTPOINT
	rm -fr $FS_MNTPOINT
fi

default_cleanup
