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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/atime/atime_common.kshlib

#
# DESCRIPTION:
# When atime=on, verify the access time for files is updated when read. It
# is available to fs and clone. To snapshot, it is unavailable.
#
# STRATEGY:
# 1. Create pool and fs.
# 2. Create '$TESTFILE' for fs.
# 3. Create snapshot and clone.
# 4. Setting atime=on on datasets except snapshot, and read '$TESTFILE'.
# 5. Expect the access time is updated on datasets except snapshot.
#

verify_runnable "both"

log_assert "Setting atime=on, the access time for files is updated when read."
log_onexit cleanup

#
# Create $TESTFILE, snapshot and clone.
#
setup_snap_clone

for dst in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCLONE $TESTPOOL/$TESTFS@$TESTSNAP
do
	typeset mtpt=$(get_prop mountpoint $dst)

	if [[ $dst == $TESTPOOL/$TESTFS@$TESTSNAP ]]; then
		mtpt=$(snapshot_mountpoint $dst)
		log_mustnot check_atime_updated $mtpt/$TESTFILE
	else
		log_must zfs set atime=on $dst
		log_must zfs set relatime=off $dst

		log_must check_atime_updated $mtpt/$TESTFILE
		log_must check_atime_updated $mtpt/$TESTFILE
	fi
done

log_pass "Verify the property atime=on passed."
