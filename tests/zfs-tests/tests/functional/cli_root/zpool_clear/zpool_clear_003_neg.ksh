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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
# Verify 'zpool clear' cannot used to spare device.
#
# STRATEGY:
# 1. Create a spare pool.
# 2. Try to clear the spare device
# 3. Verify it returns an error.
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL1 && \
                log_must zpool destroy -f $TESTPOOL1

        for file in `ls $TESTDIR/file.*`; do
		log_must rm -f $file
        done
}


log_assert "Verify 'zpool clear' cannot clear error for spare device."
log_onexit cleanup

#make raw files to create a spare pool
typeset -i i=0
while (( i < 5 )); do
	log_must mkfile $FILESIZE $TESTDIR/file.$i

	(( i = i + 1 ))
done
log_must zpool create $TESTPOOL1 raidz $TESTDIR/file.1 $TESTDIR/file.2 \
	$TESTDIR/file.3 spare $TESTDIR/file.4

log_mustnot zpool clear $TESTPOOL1 $TESTDIR/file.4

log_pass "'zpool clear' works on spare device failed as expected."
