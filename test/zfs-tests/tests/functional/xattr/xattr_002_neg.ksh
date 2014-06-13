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
#
# Trying to read a non-existent xattr should fail.
#
# STRATEGY:
#	1. Create a file
#       2. Try to read a non-existent xattr, check that an error is returned.
#

function cleanup {

	log_must $RM $TESTDIR/myfile.$$

}

log_assert "A read of a non-existent xattr fails"
log_onexit cleanup

# create a file
log_must $TOUCH $TESTDIR/myfile.$$
log_mustnot eval "$CAT $TESTDIR/myfile.$$ not-here.txt > /dev/null 2>&1"

log_pass "A read of a non-existent xattr fails"
