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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# When set property exec=off on a filesystem, processes can not be executed from
# this filesystem.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Copy '/usr/bin/ls' to the ZFS file system.
# 3. Setting exec=off on this file system.
# 4. Make sure '/usr/bin/ls' can not work in this ZFS file system.
# 5. Make sure mmap which is using the PROT_EXEC calls failed.
#

verify_runnable "both"
MMAP_EXEC=$STF_SUITE/tests/functional/exec/mmap_exec

function cleanup
{
	log_must $RM $TESTDIR/myls
}

#
# Execute and check if the return value is equal to expected.
#
# $1 expected value
# $2..$n executed item
#
function exec_n_check
{
	typeset expect_value=$1

	shift
	$@
	ret=$?
	if [[ $ret != $expect_value ]]; then
		log_fail "Unexpected return code: '$ret'"
	fi

	return 0
}

log_assert "Setting exec=off on a filesystem, processes can not be executed " \
	"from this file system."
log_onexit cleanup

log_must $CP $LS $TESTDIR/myls
log_must $ZFS set exec=off $TESTPOOL/$TESTFS

log_must exec_n_check 126 $TESTDIR/myls
log_must exec_n_check 13 $MMAP_EXEC $TESTDIR/myls

log_pass "Setting exec=off on filesystem testing passed."
