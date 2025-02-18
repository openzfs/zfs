#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs create <filesystem>' can create a ZFS filesystem in the namespace.
#
# STRATEGY:
# 1. Create a ZFS filesystem in the storage pool
# 2. Verify the filesystem created successfully
#

verify_runnable "both"


function cleanup
{
	typeset -i i=0
	while (( $i < ${#datasets[*]} )); do
		datasetexists ${datasets[$i]} && \
			destroy_dataset ${datasets[$i]} -f
		((i = i + 1))
	done

	zfs destroy -f "$TESTPOOL/with a space"
}

log_onexit cleanup

set -A datasets "$TESTPOOL/$TESTFS1" "$TESTPOOL/$LONGFSNAME" "$TESTPOOL/..." \
		"$TESTPOOL/_1234_"

log_assert "'zfs create <filesystem>' can create a ZFS filesystem in the namespace."

typeset -i i=0
while (( $i < ${#datasets[*]} )); do
	log_must zfs create ${datasets[$i]}
	datasetexists ${datasets[$i]} || \
		log_fail "zfs create ${datasets[$i]} fail."
	((i = i + 1))
done

log_must zfs create "$TESTPOOL/with a space"
log_must zfs unmount "$TESTPOOL/with a space"
log_must zfs mount "$TESTPOOL/with a space"

log_pass "'zfs create <filesystem>' works as expected."
