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
