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
# Verify that zfs share should fail with bad parameters
#
# STRATEGY:
# 1. Make an array of bad parameters
# 2. Use zfs share to share the filesystem
# 3. Verify that zfs share returns error
#

verify_runnable "both"

log_assert "zfs share fails with bad parameters"

fs=$TESTPOOL/$TESTFS
set -A badargs "A" "-A" "-" "-x" "-?" "=" "-a *" "-a"

for arg in "${badargs[@]}"; do
	log_mustnot eval "zfs share $arg $fs >/dev/null 2>&1"
done

#zfs share failed when missing arguments or invalid datasetname
for obj in "" "/$fs"; do
	log_mustnot eval "zfs share $obj >/dev/null 2>&1"
done

log_pass "zfs share fails with bad parameters as expected."
