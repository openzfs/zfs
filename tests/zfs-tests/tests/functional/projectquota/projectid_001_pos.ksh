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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
#
# DESCRIPTION:
#	Check project ID/flags can be set/inherited properly
#
#
# STRATEGY:
#	1. Create a regular file and a directory.
#	2. Set project ID on both directory and regular file.
#	3. New created subdir or regular file should inherit its parent's
#	   project ID if its parent has project inherit flag.
#	4. New created subdir should inherit its parent project's inherit flag.
#

function cleanup
{
	log_must rm -f $PRJFILE
	log_must rm -rf $PRJDIR
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

#
# e2fsprogs-1.44.4 incorrectly reports verity 'V' bit when the project 'P'
# bit is set.  Skip this test when 1.44.4 is installed to prevent failures.
#
# https://github.com/tytso/e2fsprogs/commit/7e5a95e3d
#
if lsattr -V 2>&1 | grep "lsattr 1.44.4"; then
	log_unsupported "Current e2fsprogs incorrectly reports 'V' verity bit"
fi

log_onexit cleanup

log_assert "Check project ID/flags can be set/inherited properly"

log_must touch $PRJFILE
log_must mkdir $PRJDIR

log_must chattr -p $PRJID1 $PRJFILE
log_must eval "lsattr -p $PRJFILE | grep $PRJID1 | grep -v '\-P[- ]* '"
log_must chattr -p $PRJID1 $PRJDIR
log_must eval "lsattr -pd $PRJDIR | grep $PRJID1 | grep -v '\-P[- ]* '"

log_must chattr +P $PRJDIR
log_must eval "lsattr -pd $PRJDIR | grep $PRJID1 | grep '\-P[- ]* '"

# "-1" is invalid project ID, should be denied
log_mustnot chattr -p -1 $PRJFILE
log_must eval "lsattr -p $PRJFILE | grep $PRJID1 | grep -v '\-P[- ]* '"

log_must mkdir $PRJDIR/dchild
log_must eval "lsattr -pd $PRJDIR/dchild | grep $PRJID1 | grep '\-P[- ]* '"
log_must touch $PRJDIR/fchild
log_must eval "lsattr -p $PRJDIR/fchild | grep $PRJID1"

log_must touch $PRJDIR/dchild/foo
log_must eval "lsattr -p $PRJDIR/dchild/foo | grep $PRJID1"

# not support project ID/flag on symlink
log_must ln -s $PRJDIR/dchild/foo $PRJDIR/dchild/s_foo
log_mustnot lsattr -p $PRJDIR/dchild/s_foo
log_mustnot chattr -p 123 $PRJDIR/dchild/s_foo
log_mustnot chattr +P $PRJDIR/dchild/s_foo

# not support project ID/flag on block special file
log_must mknod $PRJDIR/dchild/b_foo b 124 124
log_mustnot lsattr -p $PRJDIR/dchild/b_foo
log_mustnot chattr -p 123 $PRJDIR/dchild/b_foo
log_mustnot chattr +P $PRJDIR/dchild/b_foo

# not support project ID/flag on character special file
log_must mknod $PRJDIR/dchild/c_foo c 125 125
log_mustnot lsattr -p $PRJDIR/dchild/c_foo
log_mustnot chattr -p 123 $PRJDIR/dchild/c_foo
log_mustnot chattr +P $PRJDIR/dchild/c_foo

log_pass "Check project ID/flags can be set/inherited properly"
