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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# We verify that the special . and .. dirs work as expected for xattrs.
#
# STRATEGY:
#	1. Create a file and an xattr on that file
#	2. List the . directory, verifying the output
#	3. Verify we're unable to list the ../ directory
#

function cleanup {
	typeset file

	for file in $TEST_BASE_DIR/output.$$ $TEST_BASE_DIR/expected-output.$$ \
		$TESTDIR/myfile.$$ ; do
		log_must rm -f $file
	done
}

log_assert "special . and .. dirs work as expected for xattrs"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# listing the directory .
log_must eval "runat $TESTDIR/myfile.$$ ls  . > $TEST_BASE_DIR/output.$$"
create_expected_output  $TEST_BASE_DIR/expected-output.$$  \
    SUNWattr_ro  SUNWattr_rw  passwd
log_must diff $TEST_BASE_DIR/output.$$ $TEST_BASE_DIR/expected-output.$$
# list the directory . long form
log_must eval "runat $TESTDIR/myfile.$$ ls -a . > $TEST_BASE_DIR/output.$$"
create_expected_output  $TEST_BASE_DIR/expected-output.$$ . ..  \
    SUNWattr_ro  SUNWattr_rw  passwd
log_must diff $TEST_BASE_DIR/output.$$ $TEST_BASE_DIR/expected-output.$$

# list the directory .. expecting one file
OUTPUT=$(runat $TESTDIR/myfile.$$ ls ..)
if [ "$OUTPUT" != ".." ]
then
	log_fail "Listing the .. directory doesn't show \"..\" as expected."
fi

# verify we can't list ../
log_mustnot eval "runat $TESTDIR/myfile.$$ ls ../ > /dev/null 2>&1"

log_pass "special . and .. dirs work as expected for xattrs"
