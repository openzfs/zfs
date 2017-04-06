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

	for file in /tmp/output.$$ /tmp/expected-output.$$ \
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
log_must eval "runat $TESTDIR/myfile.$$ ls  . > /tmp/output.$$"
create_expected_output  /tmp/expected-output.$$  \
    SUNWattr_ro  SUNWattr_rw  passwd
log_must diff /tmp/output.$$ /tmp/expected-output.$$
# list the directory . long form
log_must eval "runat $TESTDIR/myfile.$$ ls -a . > /tmp/output.$$"
create_expected_output  /tmp/expected-output.$$ . ..  \
    SUNWattr_ro  SUNWattr_rw  passwd
log_must diff /tmp/output.$$ /tmp/expected-output.$$

# list the directory .. expecting one file
OUTPUT=$(runat $TESTDIR/myfile.$$ ls ..)
if [ "$OUTPUT" != ".." ]
then
	log_fail "Listing the .. directory doesn't show \"..\" as expected."
fi

# verify we can't list ../
log_mustnot eval "runat $TESTDIR/myfile.$$ ls ../ > /dev/null 2>&1"

log_pass "special . and .. dirs work as expected for xattrs"
