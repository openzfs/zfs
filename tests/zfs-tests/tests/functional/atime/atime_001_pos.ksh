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
		if is_linux; then
			log_must zfs set relatime=off $dst
		fi

		log_must zfs set atime=on $dst
		log_must check_atime_updated $mtpt/$TESTFILE
		log_must check_atime_updated $mtpt/$TESTFILE
	fi
done

log_pass "Verify the property atime=on passed."
