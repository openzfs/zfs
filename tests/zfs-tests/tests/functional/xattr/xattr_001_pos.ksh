#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

set -A args "on" "sa"

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
