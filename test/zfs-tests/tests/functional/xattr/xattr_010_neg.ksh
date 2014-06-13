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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# Verify that mkdir and various mknods fail inside the xattr namespace
#
# STRATEGY:
#	1. Create a file and add an xattr to it (to ensure the namespace exists)
#       2. Verify that mkdir fails inside the xattr namespace
#	3. Verify that various mknods fails inside the xattr namespace
#
#

function cleanup {

	log_must $RM $TESTDIR/myfile.$$
}

log_assert "mkdir, mknod fail"
log_onexit cleanup

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# Try to create directory in the xattr namespace
log_mustnot $RUNAT $TESTDIR/myfile.$$ $MKDIR foo

# Try to create a range of different filetypes in the xattr namespace
log_mustnot $RUNAT $TESTDIR/myfile.$$ $MKNOD block b 888 888

log_mustnot $RUNAT $TESTDIR/myfile.$$ $MKNOD char c

log_mustnot $RUNAT $TESTDIR/myfile.$$ $MKNOD fifo p

log_pass "mkdir, mknod fail"
