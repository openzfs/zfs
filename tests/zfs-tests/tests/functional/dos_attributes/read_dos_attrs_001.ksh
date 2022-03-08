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

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Read additional file level attributes stored in upper half of z_pflags
#
# STARTEGY:
#		1) Create a file
#		2) Execute read_dos_attributes on the file we created
#		3) Verify that read_dos_attributes exited successfully
#

verify_runnable "global"

FILETOTEST="$TESTDIR/test_read_dos_attrs.txt"

function cleanup
{
	rm -f $FILETOTEST
}

log_onexit cleanup

log_must chmod 777 $TESTDIR
log_must eval "echo 'This is a test file.' > $FILETOTEST"
log_must read_dos_attributes $FILETOTEST

log_pass "reading DOS attributes succeeded."
