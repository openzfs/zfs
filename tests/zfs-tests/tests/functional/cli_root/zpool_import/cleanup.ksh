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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

verify_runnable "global"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0

for pool in "$TESTPOOL" "$TESTPOOL1"; do
	datasetexists $pool/$TESTFS && destroy_dataset $pool/$TESTFS -Rf
	destroy_pool "$pool"
done

for dir in "$TESTDIR" "$TESTDIR1" "$DEVICE_DIR" ; do
	[[ -d $dir ]] && \
		log_must rm -rf $dir
done

log_pass
