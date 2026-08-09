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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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

	log_must rm $TESTDIR/myfile.$$
}

log_assert "mkdir, mknod fail"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# Try to create directory in the xattr namespace
log_mustnot runat $TESTDIR/myfile.$$ mkdir foo

# Try to create a range of different filetypes in the xattr namespace
log_mustnot runat $TESTDIR/myfile.$$ mknod block b 888 888

log_mustnot runat $TESTDIR/myfile.$$ mknod char c

log_mustnot runat $TESTDIR/myfile.$$ mkfifo fifo

log_pass "mkdir, mknod fail"
