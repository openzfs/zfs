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
# Copyright (c) 2013 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
#
# DESCRIPTION:
#       Check whether unprivileged user can chattr
#
#
# STRATEGY:
#       1. Create 3 files
#       2. Use chattr to make them writable, immutable and appendonly
#       3. Try to chattr with unprivileged user
#

set -A files writable immutable append

function cleanup
{
	for i in ${files[*]}; do
		log_must chattr -ia $TESTDIR/$i
		log_must rm -f $TESTDIR/$i
	done
	log_must chmod 0755 $TESTDIR
}

log_onexit cleanup

log_assert "Check whether unprivileged user can chattr"

log_must chmod 0777 $TESTDIR

log_must user_run $QUSER1 touch $TESTDIR/writable
log_must user_run $QUSER1 touch $TESTDIR/immutable
log_must user_run $QUSER1 touch $TESTDIR/append

log_must chattr -i $TESTDIR/writable
log_must chattr +i $TESTDIR/immutable
log_must chattr +a $TESTDIR/append

log_must user_run $QUSER1 chattr -i $TESTDIR/writable
log_must user_run $QUSER1 chattr -a $TESTDIR/writable
log_must user_run $QUSER1 chattr +i $TESTDIR/immutable
log_must user_run $QUSER1 chattr +a $TESTDIR/append

log_mustnot user_run $QUSER1 chattr +i $TESTDIR/writable
log_mustnot user_run $QUSER1 chattr +a $TESTDIR/writable
log_mustnot user_run $QUSER1 chattr -i $TESTDIR/immutable
log_mustnot user_run $QUSER1 chattr -a $TESTDIR/append

log_pass "Unprivileged user cannot chattr as expected"
