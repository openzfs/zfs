#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/mv_files/mv_files_common.kshlib

#
# DESCRIPTION:
# Doing a 'mv' of a large amount of files between two directories
# within a zfs filesystem works without errors.
#
# STRATEGY:
#
# 1. create a pool and a zfs filesystem
# 2. create two directories within the filesystem
# 3. create a large number of files within a directory
# 4. Move files from one directory to another and back again
# 5. validate file number
# 6. increase the number of files to $MVNUMFILES + $MVNUMINCR
# 7. repeat steps 3,4,5,6 above
# 8. verify the data integrity
#

verify_runnable "global"

function cleanup
{
	rm -f $OLDDIR/* $NEWDIR_IN_FS/* >/dev/null 2>&1
}

log_assert "Doing a 'mv' of a large amount of files within a zfs filesystem" \
	    "works without errors."

log_onexit cleanup

log_must mv_test $OLDDIR $NEWDIR_IN_FS

log_pass
