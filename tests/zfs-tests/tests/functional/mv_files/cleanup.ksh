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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mv_files/mv_files_common.kshlib

verify_runnable "global"

[[ -f $TEST_BASE_DIR/exitsZero.ksh ]] &&
	log_must rm -f $TEST_BASE_DIR/exitsZero.ksh
[[ -f $TEST_BASE_DIR/testbackgprocs.ksh ]] &&
	log_must rm -f $TEST_BASE_DIR/testbackgprocs.ksh

ismounted $TESTPOOL/$TESTFS_TGT ||log_must zfs umount $TESTPOOL/$TESTFS_TGT
log_must zfs destroy $TESTPOOL/$TESTFS_TGT

[[ -d $TESTDIR_TGT ]] && log_must rm -rf $TESTDIR_TGT

default_cleanup
