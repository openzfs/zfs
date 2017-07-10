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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

#
# DESCRIPTION:
#	Verify that user can access file/directory if acltype=posixacl.
#
# STRATEGY:
#	1. Test access to directory (mode=-wx)
#	   a. Can create file in dir
#	   b. Can't list directory
#

verify_runnable "both"
log_assert "Verify acltype=posixacl works on directory"

# Test access to DIRECTORY
log_note "Testing access to DIRECTORY"
log_must $MKDIR $TESTDIR/dir.0
log_must $SETFACL -m g:zfsgrp:wx $TESTDIR/dir.0
$GETFACL $TESTDIR/dir.0 2> /dev/null | $EGREP -q "^group:zfsgrp:-wx$"
if [ "$?" -eq "0" ]; then
	# Should be able to create file in directory
	log_must $SU staff1 -c "$TOUCH $TESTDIR/dir.0/file.0"

	# Should NOT be able to list files in directory
	log_mustnot $SU staff1 -c "$LS -l $TESTDIR/dir.0"

	log_pass "POSIX ACL mode works on directories"
else
	log_fail "Group 'zfsgrp' does not have 'rwx' as specified"
fi
