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
