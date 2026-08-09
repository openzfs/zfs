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
# Write additional file level attributes stored in upper half of z_pflags
#
# STARTEGY:
#		1) Create a file
#		2) Execute write_dos_attributes on the file we created
#		3) Verify that write_dos_attributes exited successfully
#

verify_runnable "global"

FILETOTEST="$TESTDIR/test_write_dos_attrs.txt"

function cleanup
{
	rm -f $FILETOTEST
}

log_onexit cleanup

log_must chmod 777 $TESTDIR
log_must eval "echo 'This is a test file.' > $FILETOTEST"
log_must write_dos_attributes offline $FILETOTEST
log_must write_dos_attributes nooffline $FILETOTEST

log_pass "writing DOS attributes succeeded."
