#! /bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Ensure multiple threads performing write appends to the same ZFS
# file succeed.
#
# STRATEGY:
#	1) Verify this is a multi-processor system
#	2) Create multiple threads with each appending to a file
#       3) Verify that the resulting file is the expected size
#

verify_runnable "both"

log_assert "Ensure multiple threads performing write appends to the same" \
	"ZFS file succeed"

#
# $FILE_SIZE is hardcoded into threadsappend.c and is the expected
# size of the file after all the threads have appended to it
#
typeset -i FILE_SIZE=1310720
TESTFILE='testfile-threadsappend'

#
# This test should be run on a multi-processor system because otherwise the FS
# will not be concurrently used by the threads
#
if ! is_mp; then
	log_fail "This test should be executed on a multi-processor system."
fi

#
# zfs_threadsappend tries to append to $TESTFILE using threads
# so that the resulting file is $FILE_SIZE bytes in size
#
log_must threadsappend ${TESTDIR}/${TESTFILE}

#
# Check the size of the resulting file
#
SIZE=`ls -l ${TESTDIR}/${TESTFILE} | awk '{print $5}'`
if [[ $SIZE -ne $FILE_SIZE ]]; then
	log_fail "'The length of ${TESTDIR}/${TESTFILE}' doesn't equal 1310720."
fi

log_pass "Multiple thread appends succeeded. File size as expected"
