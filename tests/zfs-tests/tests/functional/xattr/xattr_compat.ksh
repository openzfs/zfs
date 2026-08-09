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
