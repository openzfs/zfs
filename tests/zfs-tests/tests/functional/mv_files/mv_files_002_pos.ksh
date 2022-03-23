#! /bin/ksh -p
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mv_files/mv_files_common.kshlib

#
# DESCRIPTION:
# Doing a 'mv' of a large amount of files between two directories across
# two zfs filesystems works without errors.
#
# STRATEGY:
#
# 1. create a pool and two zfs filesystems
# 2. create a directory in each filesystem
# 3. create a large number of files in a directory of a filesystem
# 4. Move files from the directory to another directory in another
# filesystem and back again
# 5. validate file number
# 6. increase the number of files to $MVNUMFILES + $MVNUMINCR
# 7. repeat steps 3,4,5,6 above
# 8. verify the data integrity
#

verify_runnable "global"

function cleanup
{
	rm -f $OLDDIR/* $NEWDIR_ACROSS_FS/*  >/dev/null 2>&1
}

log_assert "Doing a 'mv' of a large amount of files across two zfs filesystems" \
	    "works without errors."

log_onexit cleanup

log_must mv_test $OLDDIR $NEWDIR_ACROSS_FS

log_pass
