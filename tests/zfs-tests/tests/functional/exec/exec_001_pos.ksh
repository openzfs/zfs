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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# When set property exec=on on a filesystem, processes can be executed from
# this filesystem.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Copy '$STF_PATH/ls' to the ZFS file system.
# 3. Setting exec=on on this file system.
# 4. Make sure '$STF_PATH/ls' can work in this ZFS file system.
# 5. Make sure mmap which is using the PROT_EXEC calls succeed.
#

verify_runnable "both"

function cleanup
{
	log_must rm $TESTDIR/myls
}

log_assert "Setting exec=on on a filesystem, processes can be executed from " \
	"this file system."
log_onexit cleanup

log_must cp $STF_PATH/ls $TESTDIR/myls
log_must zfs set exec=on $TESTPOOL/$TESTFS
log_must $TESTDIR/myls
log_must mmap_exec $TESTDIR/myls

log_pass "Setting exec=on on filesystem testing passed."
