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
#
# Creating, reading and writing xattrs on ZFS filesystems works as expected
#
# STRATEGY:
#	1. Create an xattr on a ZFS-based file using runat
#	2. Read an empty xattr directory
#       3. Write the xattr using runat and cat
#	3. Read the xattr using runat
#	4. Delete the xattr
#	5. List the xattr namespace successfully, checking for deletion
#

function cleanup {

	if [ -f $TESTDIR/myfile.$$ ]
	then
		log_must rm $TESTDIR/myfile.$$
	fi
}

set -A args "dir" "sa"

log_assert "Create/read/write/append of xattrs works"
log_onexit cleanup

for arg in ${args[*]}; do
	log_must zfs set xattr=$arg $TESTPOOL

	log_must touch $TESTDIR/myfile.$$
	create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd
	verify_write_xattr $TESTDIR/myfile.$$ passwd
	delete_xattr $TESTDIR/myfile.$$ passwd
done

log_pass "Create/read/write of xattrs works"
