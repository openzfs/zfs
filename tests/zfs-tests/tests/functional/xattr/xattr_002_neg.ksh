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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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

	log_must rm $TESTDIR/myfile.$$

}

set -A args "dir" "sa"

log_assert "A read of a non-existent xattr fails"
log_onexit cleanup

for arg in ${args[*]}; do
	log_must zfs set xattr=$arg $TESTPOOL

	# create a file
	log_must touch $TESTDIR/myfile.$$
	log_mustnot eval "cat $TESTDIR/myfile.$$ not-here.txt > /dev/null 2>&1"
done

log_pass "A read of a non-existent xattr fails"
