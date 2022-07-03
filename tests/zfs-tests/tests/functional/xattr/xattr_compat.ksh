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
# Copyright 2022 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The zfs_xattr_compat tunable and fallback works as expected.
#
# STRATEGY:
#	For both of xattr=sa and xattr=dir:
#	1. Create a filesystem with the native zfs_xattr_compat
#	2. Create a file on the filesystem and add some xattrs to it
#	3. Change the zfs_xattr_compat to the alternative setting
#	4. Verify that the xattrs can still be accessed and modified
#	5. Change zfs_xattr_compat back to the native setting
#	6. Verify that the xattrs can still be accessed and modified
#

function cleanup {
	rm -f $TESTFILE $TMPFILE
	zfs set xattr=sa $TESTPOOL/$TESTFS
	set_tunable32 XATTR_COMPAT $NATIVE_XATTR_COMPAT
}

log_assert "The zfs_xattr_compat tunable and fallback works as expected"
log_onexit cleanup

TESTFILE=$TESTDIR/testfile.$$
TMPFILE=$TEST_BASE_DIR/tmpfile.$$
NATIVE_XATTR_COMPAT=$(get_tunable XATTR_COMPAT)
ALTERNATIVE_XATTR_COMPAT=$((1 - NATIVE_XATTR_COMPAT))

for x in sa dir; do
	log_must zfs set xattr=$x $TESTPOOL/$TESTFS
	log_must touch $TESTFILE
	log_must set_xattr testattr1 value1 $TESTFILE
	log_must set_xattr testattr2 value2 $TESTFILE
	log_must set_xattr testattr3 value3 $TESTFILE
	log_must ls_xattr $TESTFILE

	log_must set_tunable32 XATTR_COMPAT $ALTERNATIVE_XATTR_COMPAT
	log_must ls_xattr $TESTFILE
	log_must eval "get_xattr testattr1 $TESTFILE > $TMPFILE"
	log_must test $(<$TMPFILE) = value1
	log_must set_xattr testattr2 newvalue2 $TESTFILE
	log_must rm_xattr testattr3 $TESTFILE
	log_must set_xattr testattr4 value4 $TESTFILE
	log_must ls_xattr $TESTFILE

	log_must set_tunable32 XATTR_COMPAT $NATIVE_XATTR_COMPAT
	log_must ls_xattr $TESTFILE
	log_must eval "get_xattr testattr1 $TESTFILE > $TMPFILE"
	log_must test $(<$TMPFILE) = value1
	log_must eval "get_xattr testattr2 $TESTFILE > $TMPFILE"
	log_must test $(<$TMPFILE) = newvalue2
	log_mustnot get_xattr testattr3 $TESTFILE
	log_must set_xattr testattr3 value3 $TESTFILE
	log_must eval "get_xattr testattr4 $TESTFILE > $TMPFILE"
	log_must test $(<$TMPFILE) = value4
	log_must ls_xattr $TESTFILE

	log_must rm $TESTFILE
done

log_pass "The zfs_xattr_compat tunable and fallback works as expected"
