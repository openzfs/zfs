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
# Copyright (c) 2016 by Delphix. All rights reserved.
# Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
#

. $STF_SUITE/tests/functional/atime/atime_common.kshlib

#
# DESCRIPTION:
# When atime=off, verify the access time for files is not updated when read.
# It is available to pool, fs snapshot and clone.
#
# STRATEGY:
# 1. Create pool, fs.
# 2. Create '$TESTFILE' for fs.
# 3. Create snapshot and clone.
# 4. Setting atime=off on dataset and read '$TESTFILE'.
# 5. Verify the access time is not updated.
#

verify_runnable "both"

log_assert "Setting atime=off, the access time for files will not be updated \
	when read."
log_onexit cleanup

#
# Create $TESTFILE, snapshot and clone.
# Same as 002 except that atime applies to root dataset (OpenZFS#8675).
#
setup_snap_clone
reset_atime

for dst in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCLONE $TESTPOOL/$TESTFS@$TESTSNAP
do
	typeset mtpt=$(get_prop mountpoint $dst)

	if [[ $dst == $TESTPOOL/$TESTFS@$TESTSNAP ]]; then
		mtpt=$(snapshot_mountpoint $dst)
	else
		log_must zfs set atime=off $(dirname $dst)
	fi

	log_mustnot check_atime_updated $mtpt/$TESTFILE
done

log_pass "Verify the property atime=off passed."
