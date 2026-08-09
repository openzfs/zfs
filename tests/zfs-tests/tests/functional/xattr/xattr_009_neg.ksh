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
# links between xattr and normal file namespace fail
#
# STRATEGY:
#	1. Create a file and add an xattr to it (to ensure the namespace exists)
#       2. Verify we're unable to create a symbolic link
#	3. Verify we're unable to create a hard link
#

function cleanup {

	log_must rm $TESTDIR/myfile.$$

}

log_assert "links between xattr and normal file namespace fail"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# Try to create a soft link from the xattr namespace to the default namespace
log_mustnot runat $TESTDIR/myfile.$$ ln -s /etc/passwd foo

# Try to create a hard link from the xattr namespace to the default namespace
log_mustnot runat $TESTDIR/myfile.$$ ln /etc/passwd foo

log_pass "links between xattr and normal file namespace fail"
