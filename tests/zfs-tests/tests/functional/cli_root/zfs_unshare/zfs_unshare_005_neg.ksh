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
# Verify that unsharing a dataset and mountpoint other than filesystem fails.
#
# STRATEGY:
# 1. Create a volume, dataset other than a ZFS file system
# 2. Verify that the datasets other than file system are not support by 'zfs unshare'.
#

verify_runnable "both"

set -A datasets \
	"$TESTPOOL" "$ZFSROOT/$TESTPOOL" \
	"$TESTPOOL/$TESTCTR" "$ZFSROOT/$TESTPOOL/$TESTCTR" \
	"$TESTPOOL/$TESTVOL" "${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL"

log_assert "Verify that unsharing a dataset other than filesystem fails."

typeset -i i=0
while (( i < ${#datasets[*]} ))
do
	log_mustnot zfs unshare ${datasets[i]}

	((i = i + 1))
done

log_pass "Unsharing datasets other than filesystem failed as expected."
