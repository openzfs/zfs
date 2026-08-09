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
