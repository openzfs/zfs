#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_share/zfs_share.cfg

#
# DESCRIPTION:
# Verify that "zfs share" with a non-existent file system fails.
#
# STRATEGY:
# 1. Make sure the NONEXISTFSNAME ZFS file system is not in 'zfs list'.
# 2. Invoke 'zfs share <file system>'.
# 3. Verify that share fails
#

verify_runnable "both"

function cleanup
{
	typeset fs
	for fs in $NONEXISTFSNAME $TESTFS ; do
		log_must unshare_fs $TESTPOOL/$fs
	done
}

typeset -i ret=0

log_assert "Verify that "zfs share" with a non-existent file system fails."

log_onexit cleanup

log_mustnot zfs list $TESTPOOL/$NONEXISTFSNAME

zfs share $TESTPOOL/$NONEXISTFSNAME
ret=$?
(( ret == 1)) || \
	log_fail "'zfs share $TESTPOOL/$NONEXISTFSNAME' " \
		"failed with an unexpected return code of $ret."

log_note "Make sure the file system $TESTPOOL/$NONEXISTFSNAME is unshared"
not_shared $TESTPOOL/$NONEXISTFSNAME || \
	log_fail "File system $TESTPOOL/$NONEXISTFSNAME is unexpectedly shared."

log_pass "'zfs share' with a non-existent file system fails."
